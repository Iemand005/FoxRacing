#pragma once
#include "ModelLoader.hpp"
#include "XRGame.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include <random>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_sdl3.h>
#include <imgui/backends/imgui_impl_opengl3.h>

#include <EditableGame.hpp>
#include <Primitives.hpp>
#include <Sensors/Accelerometer.hpp>
#include <physics/PhysicsVehicle.hpp>
#include <Graphics/VulkanDevice.hpp>
#ifdef FE_INCLUDE_OPENVR
#include <openvr/OpenVR.hpp>
#endif

class FoxRacing : public fe::EditableGame {
public:

	bool showDebugUI = false;
	bool useRectangularPlayerHitbox = true;
	bool hasWon = false;
	unsigned int mazeSeed = 0;
	std::vector<std::shared_ptr<fe::Object>> mazeWalls;

	std::vector<fe::Accelerometer> accelerometers;
	std::vector<glm::vec3> accelReadings;
	int selectedAccel = 0;
	std::shared_ptr<fe::Object> ballObject;
	std::shared_ptr<fe::Object> goalObject;

	std::vector<fe::Joystick> joysicks;

	std::shared_ptr<fe::Object> lambo;
	fe::PhysicsVehicle* carVehicle = nullptr;
	std::vector<std::shared_ptr<fe::Object>> barrierWalls;

	static constexpr int MAZE_COLS = 8;
	static constexpr int MAZE_ROWS = 8;
	static constexpr float CELL_SIZE = 2.0f;
	static constexpr float WALL_HEIGHT = 1.0f;
	static constexpr float WALL_THICK = 0.3f;
	static constexpr float BALL_RADIUS = 0.5f;

	struct MazeCell {
		bool wallN = true, wallS = true, wallE = true, wallW = true;
		bool visited = false;
	};

	MazeCell mazeGrid[MAZE_ROWS][MAZE_COLS];

	void GenerateMaze() {
		// Reset all cells
		for (int r = 0; r < MAZE_ROWS; r++)
			for (int c = 0; c < MAZE_COLS; c++)
				mazeGrid[r][c] = MazeCell();

		std::mt19937 rng(mazeSeed);
		std::uniform_int_distribution<int> dirDist(0, 3);
		int dx[] = { 0, 0, 1, -1 };
		int dz[] = { -1, 1, 0, 0 };

		auto carve = [&](auto& self, int cx, int cz) -> void {
			mazeGrid[cz][cx].visited = true;

			int dirs[] = { 0, 1, 2, 3 };
			std::shuffle(dirs, dirs + 4, rng);

			for (int d = 0; d < 4; d++) {
				int nd = dirs[d];
				int nx = cx + dx[nd];
				int nz = cz + dz[nd];
				if (nx < 0 || nx >= MAZE_COLS || nz < 0 || nz >= MAZE_ROWS) continue;
				if (mazeGrid[nz][nx].visited) continue;

				if (nd == 0) { mazeGrid[cz][cx].wallN = false; mazeGrid[nz][nx].wallS = false; }
				if (nd == 1) { mazeGrid[cz][cx].wallS = false; mazeGrid[nz][nx].wallN = false; }
				if (nd == 2) { mazeGrid[cz][cx].wallE = false; mazeGrid[nz][nx].wallW = false; }
				if (nd == 3) { mazeGrid[cz][cx].wallW = false; mazeGrid[nz][nx].wallE = false; }

				self(self, nx, nz);
			}
		};

		carve(carve, 0, 0);
	}

	glm::vec3 CellToWorld(int col, int row) {
		float totalW = MAZE_COLS * CELL_SIZE;
		float totalH = MAZE_ROWS * CELL_SIZE;
		return glm::vec3(
			col * CELL_SIZE + CELL_SIZE * 0.5f - totalW * 0.5f,
			0.0f,
			row * CELL_SIZE + CELL_SIZE * 0.5f - totalH * 0.5f
		);
	}

	glm::vec3 GetBallSpawnPos() {
		glm::vec3 pos = CellToWorld(0, 0);
		pos.y = BALL_RADIUS + 0.1f;
		return pos;
	}

	void ResetBallToSpawn() {
		if (!ballObject || !ballObject->physicsObject) return;
		glm::vec3 pos = GetBallSpawnPos();
		ballObject->state.position = pos;
		ballObject->physicsObject->SetPosition(pos);
		ballObject->physicsObject->SetLinearVelocity(glm::vec3(0.0f));
	}

	FoxRacing(fe::XRGameOptions options) : fe::EditableGame(options) {

		SetClearColor(0.1f, 0.3f, 1);
		mazeSeed = static_cast<unsigned int>(std::random_device{}());

		if (!useVulkan)
			LoadShaders("resources/shaders/VertexShader.glsl", "resources/shaders/FragmentShader.glsl");

		LoadModels();

		GetPhysicsEngine()->EnableGravity();

		accelerometers = fe::Accelerometer::EnumerateAll();
		accelReadings.resize(accelerometers.size(), glm::vec3(0.0f));
		for (size_t i = 0; i < accelerometers.size(); i++) {
			accelerometers[i].Start([this, i](const glm::vec3& accel) {
				accelReadings[i] = accel;
			});
		}
	}

	void OnPreSwap() override {}

	void RebuildPlayerPhysicsBody() {
		auto physicsEngine = GetPhysicsEngine();
		if (!player || !physicsEngine) return;

		const glm::vec3 size = useRectangularPlayerHitbox ? glm::vec3(0.4f, 1.5f, 0.4f) : glm::vec3(1.0f, 1.0f, 1.0f);
		auto newPhysics = physicsEngine->CreateObject(size, true);
		if (!newPhysics) return;

		this->player->SetPhysicsObject(std::move(newPhysics));
		if (this->player->physicsObject) {
			this->player->physicsObject->SetPosition(this->player->state.position);
		}
	}

	void AddMeshColliders(fe::Object* obj) {
		for (auto& mesh : obj->meshes) {
			std::vector<glm::vec3> positions;
			mesh->GetPositions(positions);
			if (!positions.empty()) {
				glm::mat4 world = obj->GetModelMatrix();
				std::vector<glm::vec3> worldPos;
				worldPos.reserve(positions.size());
				for (const auto& v : positions)
					worldPos.push_back(glm::vec3(world * glm::vec4(v, 1.0f)));
				auto physObj = GetPhysicsEngine()->CreateObject(worldPos, mesh->GetIndices());
				mesh->SetPhysicsObject(std::move(physObj));
			}
		}
		obj->isStatic = true;
		for (auto& child : obj->children) {
			AddMeshColliders(child.get());
		}
	}

	void CreateBarrierWalls() {
		float size = 500.0f;
		float height = 5.0f;
		float thick = 1.0f;
		glm::vec3 col(0.0f);
		auto addWall = [&](glm::vec3 pos, glm::vec3 scale) {
			auto mesh = fe::Primitives::GenerateCube(
				{fe::PlaneDirection::Front, fe::PlaneDirection::Back, fe::PlaneDirection::Left,
				 fe::PlaneDirection::Right, fe::PlaneDirection::Top, fe::PlaneDirection::Bottom},
				fe::Primitives::defaultUVs, 1.0f);
			auto wall = std::make_shared<fe::Object>(mesh);
			wall->state.position = pos;
			wall->state.scale = scale;
			wall->color = col;
			wall->isStatic = true;
			wall->SetPhysicsObject(GetPhysicsEngine()->CreateObject(scale, false));
			if (wall->physicsObject)
				wall->physicsObject->SetPosition(pos);
			scene->AddObject(wall);
			barrierWalls.push_back(wall);
		};
		float half = size * 0.5f;
		addWall(glm::vec3(0.0f, -2.0f, 0.0f), glm::vec3(size, 0.5f, size));
		addWall(glm::vec3(0.0f, height * 0.5f, -half - thick), glm::vec3(size + thick, height, thick));
		addWall(glm::vec3(0.0f, height * 0.5f, half + thick), glm::vec3(size + thick, height, thick));
		addWall(glm::vec3(-half - thick, height * 0.5f, 0.0f), glm::vec3(thick, height, size + thick));
		addWall(glm::vec3(half + thick, height * 0.5f, 0.0f), glm::vec3(thick, height, size + thick));
	}

	void ResetCar() {
		if (!lambo || !lambo->physicsObject) return;
		lambo->state.position = glm::vec3(0.0f, 5.0f, 0.0f);
		lambo->physicsObject->SetPosition(lambo->state.position);
		lambo->physicsObject->SetLinearVelocity(glm::vec3(0.0f));
		lambo->physicsObject->SetAngularVelocity(glm::vec3(0.0f));
		if (carVehicle) {
			carVehicle->SetDriverInput(0.0f, 0.0f, 0.0f, 0.0f);
		}
	}

	void ScaleObject(fe::Object* obj, float s) {
		obj->state.scale *= s;
		for (auto& child : obj->children)
			ScaleObject(child.get(), s);
	}

	void LoadModels() {
		float worldScale = 1.35f;
		auto terrain = fe::ModelLoader::LoadModel("C:/Users/Lasse/3D Objects/road_with_trees.glb");
		if (terrain) {
			scene->AddObject(terrain);
			ScaleObject(terrain.get(), worldScale);
			AddMeshColliders(terrain.get());
		}

		lambo = fe::ModelLoader::LoadModel("C:/Users/Lasse/3D Objects/1988_lamborghini_countach.glb");
		if (lambo) {
			lambo->state.position = glm::vec3(0.0f, 10.0f, 0.0f);
			scene->AddObject(lambo);
			lambo->SetPhysicsObject(GetPhysicsEngine()->CreateObject(glm::vec3(1.8f, 0.6f, 4.0f), true, true));
			lambo->visualOffset = glm::vec3(0.0f, -0.6f, 0.0f);
			if (lambo->physicsObject) {
				lambo->physicsObject->SetPosition(lambo->state.position);

				std::vector<fe::PhysicsVehicle::WheelConfig> wheels;
				auto wc = [](glm::vec3 pos, bool steer, bool driven) {
					fe::PhysicsVehicle::WheelConfig w;
					w.position = pos; w.radius = 0.35f; w.width = 0.25f;
					w.suspensionMaxLength = 0.3f;
					w.suspensionFrequency = 1.5f;
					w.suspensionDamping = 0.5f;
					w.friction = 1.0f;
					w.isSteering = steer; w.isDriven = driven;
					return w;
				};
				wheels.push_back(wc(glm::vec3(-0.75f, -0.15f, 1.5f), true, false));
				wheels.push_back(wc(glm::vec3( 0.75f, -0.15f, 1.5f), true, false));
				wheels.push_back(wc(glm::vec3(-0.75f, -0.15f, -1.5f), false, true));
				wheels.push_back(wc(glm::vec3( 0.75f, -0.15f, -1.5f), false, true));
				carVehicle = GetPhysicsEngine()->CreateVehicle(lambo->physicsObject.get(), wheels);
			}
		}

		CreateBarrierWalls();
	}

	bool freeCamera = false;
	float orbitYaw = 0.0f;
	float orbitPitch = -20.0f;
	float orbitDistance = 10.0f;

	void SyncCameraToCar() {
		if (!lambo || freeCamera) return;
		glm::vec3 carPos = lambo->state.position;
		glm::mat4 rot = glm::rotate(glm::mat4(1.0f), glm::radians(orbitYaw), glm::vec3(0.0f, 1.0f, 0.0f));
		rot = glm::rotate(rot, glm::radians(orbitPitch), glm::vec3(1.0f, 0.0f, 0.0f));
		glm::vec3 offset = glm::vec3(rot * glm::vec4(0.0f, 0.0f, orbitDistance, 1.0f));
		camera->SetPos(carPos + offset);
		camera->LookAt(carPos);
	}

	void ProcessInput() {
		SDL_Event event;
		fe::SDLWindow *window = (fe::SDLWindow*)this->window.get();
		window->UpdateJoysticks();
		while (window->PollSDLEvent(&event)) {
			ImGui_ImplSDL3_ProcessEvent(&event);
			auto io = ImGui::GetIO();
			switch (event.type) {
				case SDL_EVENT_QUIT:
					window->PrepareClose();
					break;
				case SDL_EVENT_MOUSE_BUTTON_DOWN:
					if (event.button.button == SDL_BUTTON_LEFT && !io.WantCaptureMouse) {
						window->StartMouseCapture();
					}
					break;
			case SDL_EVENT_WINDOW_RESIZED:
			case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
				break;
				case SDL_EVENT_MOUSE_WHEEL:
				{
					orbitDistance -= event.wheel.y * 2.0f;
					orbitDistance = std::clamp(orbitDistance, 2.0f, 50.0f);
					break;
				}
				case SDL_EVENT_MOUSE_MOTION:
				{
					if (!window->IsCapturingMouse()) break;
					float sensitivity = 0.2f;
					orbitYaw -= event.motion.xrel * sensitivity;
					orbitPitch -= event.motion.yrel * sensitivity;
					orbitPitch = std::clamp(orbitPitch, -89.0f, 89.0f);
					if (freeCamera) {
						glm::vec3 dir;
						dir.x = cos(glm::radians(orbitYaw)) * cos(glm::radians(orbitPitch));
						dir.y = sin(glm::radians(orbitPitch));
						dir.z = sin(glm::radians(orbitYaw)) * cos(glm::radians(orbitPitch));
						camera->setFront(glm::normalize(dir));
					}
					break;
				}
				case SDL_EVENT_KEY_DOWN:
					if (event.key.key == SDLK_F11) {
						window->ToggleFullscreen();
					}
					else if (event.key.key == SDLK_F3) {
						showDebugUI = !showDebugUI;
					}
					else if (event.key.key == SDLK_F2) {
						freeCamera = !freeCamera;
						window->StartMouseCapture();
					}
					break;
			}
		}

		if (!freeCamera && carVehicle) {
			float forward = 0.0f, right = 0.0f, brake = 0.0f;
			if (window->IsKeyDown(SDL_SCANCODE_W) || window->IsKeyDown(SDL_SCANCODE_UP)) forward = 1.0f;
			if (window->IsKeyDown(SDL_SCANCODE_S) || window->IsKeyDown(SDL_SCANCODE_DOWN)) forward = -0.5f;
			if (window->IsKeyDown(SDL_SCANCODE_A) || window->IsKeyDown(SDL_SCANCODE_LEFT)) right = -1.0f;
			if (window->IsKeyDown(SDL_SCANCODE_D) || window->IsKeyDown(SDL_SCANCODE_RIGHT)) right = 1.0f;
			if (window->IsKeyDown(SDL_SCANCODE_SPACE)) brake = 1.0f;
			carVehicle->SetDriverInput(forward, right, brake, 0.0f);
		}

		if (window->IsKeyDown(SDL_SCANCODE_ESCAPE)) window->StopMouseCapture();
		if (ImGui::GetIO().WantCaptureMouse) window->StopMouseCapture();
	}

	void CheckWinCondition() {
		if (hasWon || !ballObject || !goalObject) return;
		float dist = glm::length(ballObject->state.position - goalObject->state.position);
		if (dist < BALL_RADIUS + 0.8f) {
			hasWon = true;
		}

		if (!hasWon && ballObject && ballObject->physicsObject) {
			float halfBoard = std::max(MAZE_COLS, MAZE_ROWS) * CELL_SIZE * 0.5f + 1.0f;
			auto& bp = ballObject->state.position;
			if (bp.y < -5.0f || bp.x < -halfBoard || bp.x > halfBoard || bp.z < -halfBoard || bp.z > halfBoard) {
				ResetBallToSpawn();
			}
		}
	}

	void Run() {
		auto window = this->GetWindow<fe::SDLWindow>();
		window->Show();
		window->DisableVSync();

		camera->SetAspect(camera->aspect);

		if (lambo) {
			lambo->state.position = glm::vec3(0.0f, 5.0f, 0.0f);
			if (lambo->physicsObject)
				lambo->physicsObject->SetPosition(lambo->state.position);
		}

		while (!window->ShouldClose()) {
			ProcessInput();

			GetPhysicsEngine()->SetGravity(glm::vec3(0.0f, -9.81f, 0.0f));

			if (!freeCamera)
				SyncCameraToCar();

			if (freeCamera) {
				int freeCamSpeed = 10;
				double dt = fpsCounter.deltaTime;
				float spd = freeCamSpeed * dt;
				glm::vec3 cp = camera->GetPos();
				glm::vec3 right = glm::normalize(glm::cross(camera->front, camera->up));
				if (window->IsKeyDown(SDL_SCANCODE_W)) cp += camera->front * spd;
				if (window->IsKeyDown(SDL_SCANCODE_S)) cp -= camera->front * spd;
				if (window->IsKeyDown(SDL_SCANCODE_A)) cp -= right * spd;
				if (window->IsKeyDown(SDL_SCANCODE_D)) cp += right * spd;
				if (window->IsKeyDown(SDL_SCANCODE_SPACE)) cp += camera->up * spd;
				if (window->IsKeyDown(SDL_SCANCODE_LSHIFT)) cp -= camera->up * spd;
				camera->SetPos(cp);
			}

			if (lambo && lambo->state.position.y < -10.0f)
				ResetCar();

			Update();
			Redraw();
		}

		Destroy();
	}

	void InitUI() override {}

	void DrawUI() override {
		if (!showDebugUI && !hasWon) return;
		BeginFrame();

		if (showDebugUI) {
			DrawDebugUI();

			if (!accelerometers.empty()) {
				ImGui::Begin("Accelerometers");

				for (size_t i = 0; i < accelerometers.size(); i++) {
					bool isSelected = ((int)i == selectedAccel);
					if (ImGui::Selectable((accelerometers[i].GetName() + "##" + std::to_string(i)).c_str(), isSelected)) {
						selectedAccel = (int)i;
					}
					ImGui::SameLine();
					ImGui::TextDisabled("(%.4f, %.4f, %.4f)", accelReadings[i].x, accelReadings[i].y, accelReadings[i].z);

					if (isSelected) {
						ImGui::Indent();
						ImGui::Text("X: %.4f", accelReadings[i].x);
						ImGui::Text("Y: %.4f", accelReadings[i].y);
						ImGui::Text("Z: %.4f", accelReadings[i].z);
						if (ImGui::Button(("Calibrate##" + std::to_string(i)).c_str())) {
							accelerometers[i].Calibrate();
						}
						ImGui::Unindent();
					}
					ImGui::Separator();
				}

				ImGui::End();

				ImGui::Begin("Joysticks");
				{
					for (auto &boystick : joysicks) {

					}
				}
				ImGui::End();
			}
		}

		EndFrame();
	}
};
