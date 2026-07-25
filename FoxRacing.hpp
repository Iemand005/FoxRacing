#pragma once
#include "ModelLoader.hpp"
#include "XRGame.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/quaternion.hpp>

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

	static bool s_requestRestart;
	static bool s_restartUseVulkan;

	bool showDebugUI = false;

	std::vector<fe::Accelerometer> accelerometers;
	std::vector<glm::vec3> accelReadings;
	int selectedAccel = 0;

	std::shared_ptr<fe::Object> lambo;
	fe::PhysicsVehicle* carVehicle = nullptr;
	std::vector<std::shared_ptr<fe::Object>> barrierWalls;

	glm::vec3 prevVelocity = glm::vec3(0.0f);
	float collisionHapticTimer = 0.0f;
	float collisionHapticStrength = 0.0f;

	FoxRacing(fe::XRGameOptions options) : fe::EditableGame(options) {

		SetClearColor(0.1f, 0.3f, 1);

		if (!options.useVulkan)
			LoadShaders("resources/shaders/VertexShader.glsl", "resources/shaders/FragmentShader.glsl");

		RefreshJoysticks();
		LoadModels();

		GetPhysicsFactory()->EnableGravity();

		accelerometers = fe::Accelerometer::EnumerateAll();
		accelReadings.resize(accelerometers.size(), glm::vec3(0.0f));
		for (size_t i = 0; i < accelerometers.size(); i++) {
			accelerometers[i].Start([this, i](const glm::vec3& accel) {
				accelReadings[i] = accel;
			});
		}
	}

	void OnPreSwap() override {}

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
				auto physObj = GetPhysicsFactory()->CreateObject(worldPos, mesh->GetIndices());
				mesh->SetPhysicsObject(std::move(physObj));
			}
		}
		obj->isStatic = true;
		for (auto& child : obj->children) {
			AddMeshColliders(child.get());
		}
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
		// float worldScale = 1.35f;
		// auto terrain = fe::ModelLoader::LoadModel("C:/Users/Lasse/3D Objects/road_with_trees.glb");
		float worldScale = 1.0f;
		auto terrain = fe::ModelLoader::LoadModel("C:/Users/Lasse/3D Objects/shanghai_compressed.glb");
		if (terrain) {
			terrain->SetPosition({0,5,0});
			scene->AddObject(terrain);
			ScaleObject(terrain.get(), worldScale);
			AddMeshColliders(terrain.get());
		}

		// lambo = fe::ModelLoader::LoadModel("C:/Users/Lasse/3D Objects/1988_lamborghini_countach.glb");
		lambo = fe::ModelLoader::LoadModel("C:/Users/Lasse/3D Objects/2008 Mitsubishi Lancer Evolution X.glb");
		if (lambo) {
			lambo->state.position = glm::vec3(0.0f, 10.0f, 0.0f);
			scene->AddObject(lambo);
			lambo->SetPhysicsObject(GetPhysicsFactory()->CreateObject(glm::vec3(1.8f, 0.6f, 4.0f), true, true));
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
				carVehicle = GetPhysicsFactory()->CreateVehicle(lambo->physicsObject.get(), wheels);
				carVehicle->SetMaxPitchRollAngle(45);
				if (!joysticks.empty() && joysticks[0].IsHaptic()) {
					SDL_HapticDirection dir{};
					dir.type = SDL_HAPTIC_CARTESIAN;
					dir.dir[0] = 1;
					joysticks[0].constEffectId = joysticks[0].CreateConstantEffect(0, dir);
					joysticks[0].RunEffect(joysticks[0].constEffectId);
					joysticks[0].periodicEffectId = joysticks[0].CreatePeriodicEffect(SDL_HAPTIC_SINE, 0, 20000);
					joysticks[0].RunEffect(joysticks[0].periodicEffectId);
				}
			}
		}

	}

	bool freeCamera = false;
	bool cameraLockedToCar = true;
	float orbitYaw = 0.0f;
	float orbitPitch = -20.0f;
	float orbitDistance = 10.0f;

	void SyncCameraToCar() {
		if (!lambo || freeCamera) return;
		glm::vec3 carPos = lambo->state.position;
		glm::vec3 offset;
		if (cameraLockedToCar && lambo->physicsObject) {
			glm::quat carQuat = lambo->physicsObject->GetRotation();
			glm::quat pitchQuat = glm::angleAxis(glm::radians(orbitPitch), glm::vec3(1.0f, 0.0f, 0.0f));
			offset = carQuat * pitchQuat * glm::vec3(0.0f, 0.0f, -orbitDistance);
		} else {
			glm::mat4 rot = glm::rotate(glm::mat4(1.0f), glm::radians(orbitYaw), glm::vec3(0.0f, 1.0f, 0.0f));
			rot = glm::rotate(rot, glm::radians(orbitPitch), glm::vec3(1.0f, 0.0f, 0.0f));
			offset = glm::vec3(rot * glm::vec4(0.0f, 0.0f, orbitDistance, 1.0f));
		}
		camera->SetPos(carPos + offset);
		camera->LookAt(carPos);
	}

	void ProcessInput() {
		SDL_Event event;
		auto window = GetWindow<fe::SDLWindow>();
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
					orbitYaw += (freeCamera ? 1 : -1) * event.motion.xrel * sensitivity;
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
				else if (event.key.key == SDLK_R) {
					ResetCar();
				}
				break;
			}
		}

		if (!freeCamera && carVehicle) {
			float forward = 0.0f, right = 0.0f, brake = 0.0f, handbrake = 0.0f;

			if (!joysticks.empty()) {
				auto& joy = joysticks[0];
				right = joy.GetAxis(0);
				float rawGas = joy.GetAxis(1);
				float gas = (1.0f - rawGas) * 0.5f;
				forward = gas > 0.02f ? gas : 0.0f;
				float rev = std::max(0.0f, -joy.GetAxis(2));
				if (rev > 0.1f) forward = -rev;
			}

			if (window->IsKeyDown(SDL_SCANCODE_W) || window->IsKeyDown(SDL_SCANCODE_UP)) forward = std::max(forward, 1.0f);
			if (window->IsKeyDown(SDL_SCANCODE_S) || window->IsKeyDown(SDL_SCANCODE_DOWN)) forward = std::min(forward, -0.5f);
			if (window->IsKeyDown(SDL_SCANCODE_A) || window->IsKeyDown(SDL_SCANCODE_LEFT)) right = std::min(right, -1.0f);
			if (window->IsKeyDown(SDL_SCANCODE_D) || window->IsKeyDown(SDL_SCANCODE_RIGHT)) right = std::max(right, 1.0f);
			if (window->IsKeyDown(SDL_SCANCODE_LSHIFT)) brake = 1.0f;
			if (window->IsKeyDown(SDL_SCANCODE_SPACE)) handbrake = 1.0f;
			carVehicle->SetDriverInput(forward, right, brake, handbrake);
		}

		if (window->IsKeyDown(SDL_SCANCODE_ESCAPE)) window->StopMouseCapture();
		if (ImGui::GetIO().WantCaptureMouse) window->StopMouseCapture();
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

		SyncCameraToCar();

		while (!window->ShouldClose()) {
			ProcessInput();

			GetPhysicsFactory()->SetGravity(glm::vec3(0.0f, -9.81f, 0.0f));

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

			if (carVehicle && lambo && lambo->physicsObject) {
				glm::vec3 vel = lambo->physicsObject->GetLinearVelocity();
				float speed = glm::length(vel);
				float downforce = speed * speed * 15.0f;
				if (downforce > 1.0f)
					lambo->physicsObject->AddForce(glm::vec3(0.0f, -downforce, 0.0f));

				glm::vec3 velDelta = vel - prevVelocity;
				float impact = glm::length(velDelta);
				if (impact > 1.5f) {
					float strength = std::clamp((impact - 1.5f) / 10.0f, 0.0f, 1.0f);
					collisionHapticStrength = std::max(collisionHapticStrength, strength);
					collisionHapticTimer = 0.4f;
				}
				prevVelocity = vel;
			}

			if (collisionHapticTimer > 0.0f && fpsCounter.deltaTime > 0.0) {
				collisionHapticTimer -= static_cast<float>(fpsCounter.deltaTime);
				if (collisionHapticTimer <= 0.0f) {
					collisionHapticTimer = 0.0f;
					collisionHapticStrength = 0.0f;
				} else {
					collisionHapticStrength *= 0.92f;
				}
			}

			if (!joysticks.empty() && carVehicle && fpsCounter.deltaTime > 0.0) {
				auto& joy = joysticks[0];
				float dt = static_cast<float>(fpsCounter.deltaTime);
				int nw = carVehicle->GetNumWheels();
				float latForce = 0.0f;
				float suspSum = 0.0f;
				int latCount = 0;
				for (int i = 0; i < nw; ++i) {
					auto f = carVehicle->GetWheelForce(i);
					if (f.hasContact) {
						if (i < 2) { latForce += f.lateralLambda / dt; latCount++; }
						suspSum += f.suspensionLambda / dt;
					}
				}
				if (latCount > 0) latForce /= latCount;
				float steer = joy.GetAxis(0);
				float centering = -steer * 0.4f;
				float align = std::clamp(latForce / 4000.0f, -1.0f, 1.0f) * 0.6f;
				float ffb = std::clamp(centering + align, -1.0f, 1.0f);
				if (collisionHapticStrength > 0.01f)
					ffb = std::clamp(ffb + collisionHapticStrength, -1.0f, 1.0f);
				if (joy.constEffectId < 0 && joy.IsHaptic()) {
					SDL_HapticDirection dir{};
					dir.type = SDL_HAPTIC_CARTESIAN;
					dir.dir[0] = 1;
					joy.constEffectId = joy.CreateConstantEffect(0, dir);
					if (joy.constEffectId >= 0) joy.RunEffect(joy.constEffectId);
				}
				UpdateJoystickConstantForce(0, static_cast<Sint16>(ffb * 16384.0f));
				float rpm = carVehicle->GetEngineRPM();
				float rpmFactor = std::clamp((rpm - 1000.0f) / 2000.0f, 0.0f, 1.0f);
				float roadFactor = std::clamp(suspSum / 5000.0f / nw, 0.0f, 1.0f);
				float mag = std::clamp((rpmFactor * 0.5f + roadFactor * 0.5f) * 16384.0f, 0.0f, 16384.0f);
				if (collisionHapticStrength > 0.01f)
					mag = std::clamp(mag + collisionHapticStrength * 12000.0f, 0.0f, 16384.0f);
				if (joy.periodicEffectId < 0 && joy.IsHaptic()) {
					joy.periodicEffectId = joy.CreatePeriodicEffect(SDL_HAPTIC_SINE, 0, 20000);
					if (joy.periodicEffectId >= 0) joy.RunEffect(joy.periodicEffectId);
				}
				UpdateJoystickPeriodicEffect(0, static_cast<Sint16>(mag));
			}

			Redraw();
		}

		Destroy();
	}

	void InitUI() override {}

	void DrawUI() override {
		if (!showDebugUI) return;
		BeginFrame();

		if (showDebugUI) {
			DrawDebugUI();

			ImGui::Begin("Camera");
			ImGui::Checkbox("Lock behind car", &cameraLockedToCar);
			ImGui::Text("Yaw: %.1f Pitch: %.1f Dist: %.1f", orbitYaw, orbitPitch, orbitDistance);
			ImGui::End();

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


			}
		}

		EndFrame();
	}
};
