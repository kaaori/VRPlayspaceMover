
#include "cxxopts.hpp"

#include <iostream>
#include <algorithm>
#include <string>
#include <thread>
#include <openvr.h>
#include <vrinputemulator.h>
#include <vector>
#define GLM_SWIZZLE
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define PLAYSPACE_MOVER_VERSION "v0.0.1"

static vr::IVRSystem* m_VRSystem;
static vrinputemulator::VRInputEmulator inputEmulator;
static glm::mat4 offset;
static glm::mat4 lastOffset;
static int currentFrame;
static vr::TrackedDevicePose_t devicePoses[vr::k_unMaxTrackedDeviceCount];
static glm::vec3 devicePos[vr::k_unMaxTrackedDeviceCount];
static glm::vec3 deviceLastPos[vr::k_unMaxTrackedDeviceCount];
static glm::vec3 deviceBaseOffsets[vr::k_unMaxTrackedDeviceCount];
static std::vector<uint32_t> virtualDeviceIndexes;

void Help() {
    std::cout << "PlayspaceMover " << PLAYSPACE_MOVER_VERSION << "\n";
    std::cout << "\n";
    std::cout << "Copyright (C) 2017 Dalton Nell, PlayspaceMover Contributors\n";
    std::cout << "(https://github.com/naelstrof/VRPlayspaceMover/graphs/contributors)\n";
    std::cout << "Usage: VRPlayspaceMover [options]\n";
    std::cout << "\n";
    std::cout << "PlayspaceMover is an application that queries for a button press from\n";
    std::cout << "VR devices and moves the playspace based on it.\n";
    std::cout << "\n";
    std::cout << "-h, --help                    Print help and exit\n";
    std::cout << "-v, --version                 Print version and exit\n";
    std::cout << "Options\n";
    std::cout << "  -l, --leftButtonMask=INT\n";
    std::cout << "                              Button mask that represents which button\n";
    std::cout << "                              to detect on the left controller as an integer.\n";
    std::cout << "                              (See Button Mappings for masks).\n";
    std::cout << "  -r, --rightButtonMask=INT\n";
    std::cout << "                              Button mask that represents which button\n";
    std::cout << "                              to detect on the right controller as an integer.\n";
    std::cout << "                              (See Button Mappings for masks).\n";
    std::cout << "  --resetButtonMask=INT\n";
    std::cout << "                              Button mask that represents which buttons\n";
    std::cout << "                              need to be held on BOTH controllers to reset\n";
    std::cout << "                              the playspace.\n";
    std::cout << "                              (See Button Mappings for masks).\n";
    std::cout << "Examples\n";
    std::cout << "    $ # Moves the playspace with ONLY A/X on Oculus.\n";
    std::cout << "    $ PlayspaceMover -l 128 -r 128\n";
    std::cout << "\n";
    std::cout << "Button Mappings\n";
    std::cout << "  We take as integers as a button mask, but they actually represent a bitmask.\n";
    std::cout << "  You'll have to exercise your CompSci brain to generate these masks. Each\n";
    std::cout << "  button is represented by a bit in a 32bit integer. Bit number 7 (1000000)\n";
    std::cout << "  would be 2^7, which is 128 as an integer. Button number 7 also happens to\n";
    std::cout << "  be the A and X buttons on the Oculus controllers. Therefore setting either\n";
    std::cout << "  button mask to `128` would make it so only the A or X button activated...\n";
    std::cout << "  Similarly, you can combine bits, so if you wanted button 2 and button 7\n";
    std::cout << "  to work with it, you could pass in `130` (2^2 + 2^7), then either would\n";
    std::cout << "  work!\n";
    std::cout << "  Below is a list of some known button masks (The mask is what you supply!).\n";
    std::cout << "    Oculus Masks    Button   Bit   Mask\n";
    std::cout << "                      A/X      7     128\n";
    std::cout << "                      B/Y      1     2\n";
    std::cout << "\n";
    std::cout << "    Vive Masks      Button   Bit   Mask\n";
    std::cout << "                      Menu     1     2\n";
    std::cout << "                      Grip     2     4\n";
    std::cout << "\n";
    std::cout << "Tips\n";
    std::cout << "    * Restarting the app resets your playspace!\n";
    std::cout << "    * VR Input Emulator has a log file that can be dozens of gigabytes if\n";
    std::cout << "you're on Oculus, it's in your SteamVR folder under drivers. Set it to\n";
    std::cout << "read-only to keep it from growing indefinitely.\n";
}

void updateVirtualDevices() {
    int count = inputEmulator.getVirtualDeviceCount();
    if (virtualDeviceIndexes.size() != count) {
        virtualDeviceIndexes.clear();
        for (uint32_t deviceIndex = 0; deviceIndex < vr::k_unMaxTrackedDeviceCount; deviceIndex++) {
            try {
                virtualDeviceIndexes.push_back(inputEmulator.getVirtualDeviceInfo(deviceIndex).openvrDeviceId);
            } catch (vrinputemulator::vrinputemulator_exception e) {
                //skip
            }
        }
    }
}

bool isVirtualDevice( uint32_t deviceIndex ) {
    if (virtualDeviceIndexes.empty()) { return false; }
    return std::find(virtualDeviceIndexes.begin(), virtualDeviceIndexes.end(), deviceIndex) != virtualDeviceIndexes.end();
}

void updatePositions() {
    float fSecondsSinceLastVsync;
    vr::VRSystem()->GetTimeSinceLastVsync(&fSecondsSinceLastVsync, NULL);
    float fDisplayFrequency = vr::VRSystem()->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float);
    float fFrameDuration = 1.f / fDisplayFrequency;
    float fVsyncToPhotons = vr::VRSystem()->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_SecondsFromVsyncToPhotons_Float);
    float fPredictedSecondsFromNow = fFrameDuration - fSecondsSinceLastVsync + fVsyncToPhotons;
    vr::VRSystem()->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseRawAndUncalibrated, fPredictedSecondsFromNow, devicePoses, vr::k_unMaxTrackedDeviceCount);
    for (uint32_t deviceIndex = 0; deviceIndex < vr::k_unMaxTrackedDeviceCount; deviceIndex++) {
        if (!vr::VRSystem()->IsTrackedDeviceConnected(deviceIndex)) {
            continue;
        }
        vr::TrackedDevicePose_t* pose = devicePoses + deviceIndex;
        vr::HmdMatrix34_t* poseMat = &(pose->mDeviceToAbsoluteTracking);
        if (pose->bPoseIsValid && pose->bDeviceIsConnected) {
            deviceLastPos[deviceIndex] = devicePos[deviceIndex];
            devicePos[deviceIndex] = glm::vec3(poseMat->m[0][3], poseMat->m[1][3], poseMat->m[2][3]);
        }
    }
}

bool checkAll(uint64_t button, uint64_t mask) {
    bool ret = mask ? 1 : 0;
    for (int i = 0; i < 64; i++) {
        if (mask >> i & 1) {
            ret = ret && (button & (mask & ((uint64_t)1 << i)));
        }
    }
    return ret;
}

void updateOffset(unsigned int leftButtonMask, unsigned int rightButtonMask, unsigned int resetButtonMask) {
    vr::VRControllerState_t leftButtons,rightButtons;
    leftButtons.ulButtonPressed = 0;
    rightButtons.ulButtonPressed = 0;
    glm::vec3 delta = glm::vec3(0, 0, 0);
    float count = 0.f;
    auto leftId = vr::VRSystem()->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);
    if (leftId != vr::k_unTrackedDeviceIndexInvalid ) {
        vr::VRSystem()->GetControllerState(leftId, &leftButtons, sizeof(vr::VRControllerState_t));
        if (leftButtons.ulButtonPressed & leftButtonMask ) {
            delta += devicePos[leftId] - deviceLastPos[leftId];
            count++;
        }
    }
    auto rightId = vr::VRSystem()->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_RightHand);
    if (rightId != vr::k_unTrackedDeviceIndexInvalid ) {
        vr::VRSystem()->GetControllerState(rightId, &rightButtons, sizeof(vr::VRControllerState_t));
        if (rightButtons.ulButtonPressed & rightButtonMask ) {
            delta += devicePos[rightId] - deviceLastPos[rightId];
            count++;
        }
    }

    if (count) {
        delta /= count;
    }

    delta = glm::clamp(delta, glm::vec3(-0.1f), glm::vec3(0.1f));

    lastOffset = offset;
    if (checkAll(leftButtons.ulButtonPressed, resetButtonMask) && checkAll(rightButtons.ulButtonPressed, resetButtonMask)) {
        offset = glm::mat4(1);
    } else {
        offset = glm::translate(offset, -delta);
    }
    //offset = glm::rotate(offset, 0.001f, glm::vec3(0, 1, 0));
}

void Move() {
    for (uint32_t deviceIndex = 0; deviceIndex < vr::k_unMaxTrackedDeviceCount; deviceIndex++) {
        if (!vr::VRSystem()->IsTrackedDeviceConnected(deviceIndex)) {
            continue;
        }
        // Virtual devices need to be moved half as much, don't ask me why
        glm::vec3 oldpos = (glm::inverse(lastOffset)*glm::vec4(devicePos[deviceIndex], 1.f)).xyz();
        glm::vec3 newpos = (offset * glm::vec4(oldpos, 1.f)).xyz();
        devicePos[deviceIndex] = newpos;
        glm::vec3 delta = deviceBaseOffsets[deviceIndex];
        if (isVirtualDevice(deviceIndex)) {
			delta += (newpos - oldpos)*.5f;
        } else {
			delta += (newpos - oldpos);
        }
        vr::HmdVector3d_t copy;
        copy.v[0] = delta.x;
        copy.v[1] = delta.y;
        copy.v[2] = delta.z;
        inputEmulator.setWorldFromDriverTranslationOffset(deviceIndex, copy, false);
        //glm::fquat quat = glm::quat_cast(offset);
        //vr::HmdQuaternion_t quatCopy;
        //quatCopy.w = quat.w;
        //quatCopy.x = quat.x;
        //quatCopy.y = quat.y;
        //quatCopy.z = quat.z;
        //inputEmulator.setDriverRotationOffset(deviceIndex, quatCopy, false);
    }
}

void updateBaseOffsets() {
    for (uint32_t deviceIndex = 0; deviceIndex < vr::k_unMaxTrackedDeviceCount; deviceIndex++) {
        if (!vr::VRSystem()->IsTrackedDeviceConnected(deviceIndex)) {
            deviceBaseOffsets[deviceIndex] = glm::vec3(0);
            continue;
        }
        vrinputemulator::DeviceOffsets data;
        inputEmulator.enableDeviceOffsets(deviceIndex, true, false);
        inputEmulator.getDeviceOffsets(deviceIndex, data);
        glm::vec3 offset;
        offset.x = (float)data.worldFromDriverTranslationOffset.v[0];
        offset.y = (float)data.worldFromDriverTranslationOffset.v[1];
        offset.z = (float)data.worldFromDriverTranslationOffset.v[2];
        deviceBaseOffsets[deviceIndex] = offset;
    }
}

int app( int argc, const char** argv ) {
    cxxopts::Options options("PlayspaceMover", "Lets you grab your playspace and move it.");
    options.add_options()
        ("h,help", "Prints help.")
        ("v,version", "Prints version.")
        ("l,leftButtonMask", "Specifies the buttons that trigger the playspace grab.", cxxopts::value<unsigned int>()->default_value("130"))
        ("r,rightButtonMask", "Specifies the buttons that trigger the playspace grab.", cxxopts::value<unsigned int>()->default_value("130"))
        ("resetButtonMask", "Specifies the buttons that trigger a playspace reset.", cxxopts::value<unsigned int>()->default_value("0"))
        ;

    auto result = options.parse(argc, argv);

    if (result["help"].as<bool>()) {
        Help();
        return 0;
    }
    if (result["version"].as<bool>()) {
        std::cout << PLAYSPACE_MOVER_VERSION << "\n";
        return 0;
    }

    // Initialize stuff
    vr::EVRInitError error = vr::VRInitError_Compositor_Failed;
    std::cout << "Looking for SteamVR..." << std::flush;
    while (error != vr::VRInitError_None) {
        m_VRSystem = vr::VR_Init(&error, vr::VRApplication_Background);
        if (error != vr::VRInitError_None) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    std::cout << "Success!\n";
    std::cout << "Looking for VR Input Emulator..." << std::flush;
    while (true) {
        try {
            inputEmulator.connect();
            break;
        }
        catch (vrinputemulator::vrinputemulator_connectionerror e) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
    }
    std::cout << "Success!\n";

    offset = glm::mat4x4(1);
    lastOffset = offset;

    updateBaseOffsets();
    std::cout << "READY! Use me in VR!\n" << std::flush;

    // Main loop
    bool running = true;
    while (running) {
        if (vr::VRCompositor() != NULL) {
            vr::Compositor_FrameTiming t;
            t.m_nSize = sizeof(vr::Compositor_FrameTiming);
            bool hasFrame = vr::VRCompositor()->GetFrameTiming(&t, 0);
            if (hasFrame && currentFrame != t.m_nFrameIndex) {
                currentFrame = t.m_nFrameIndex;
                updateVirtualDevices();
                updatePositions();
                updateOffset(result["leftButtonMask"].as<unsigned int>(), result["rightButtonMask"].as<unsigned int>(), result["resetButtonMask"].as<unsigned int>());
                Move();
                vr::ETrackedPropertyError errProp;
                int microsecondWait;
                float flDisplayFrequency = vr::VRSystem()->GetFloatTrackedDeviceProperty(0, vr::Prop_DisplayFrequency_Float, &errProp);
                if (flDisplayFrequency) {
                    float flSecondsPerFrame = 1.0f / flDisplayFrequency;
                    microsecondWait = (int)(flSecondsPerFrame * 1000.f * 1000.f);
                } else {
                    microsecondWait = (int)(t.m_flCompositorIdleCpuMs*1000.f);
                }
                std::this_thread::sleep_for(std::chrono::microseconds(glm::clamp(microsecondWait, 11111, 22222)));
            }
        }
    }
    return 0;
}

int main(int argc, const char** argv) {
    try {
        return app(argc, argv);
    } catch (cxxopts::OptionException& e) {
        std::cerr << "Parse error: " << e.what() << "\n";
        std::cerr << "Try using the --help parameter!\n";
    }
    return 1;
}