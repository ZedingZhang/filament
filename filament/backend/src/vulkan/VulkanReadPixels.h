/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TNT_FILAMENT_BACKEND_VULKANREADPIXELS_H
#define TNT_FILAMENT_BACKEND_VULKANREADPIXELS_H

#include "vulkan/memory/ResourcePointer.h"

#include <private/backend/Driver.h>

#include <bluevk/BlueVK.h>

#include <utils/Condition.h>
#include <utils/Invocable.h>
#include <utils/Mutex.h>
#include <utils/compiler.h>

#include <math/vec4.h>

#include <functional>
#include <memory>
#include <queue>
#include <thread>
#include <vector>

namespace filament::backend {

struct VulkanContext;
struct VulkanRenderTarget;
struct VulkanTexture;

class VulkanReadPixels {
public:
    // A helper class that runs tasks on a separate thread.
    class TaskHandler {
    public:
        // A task is invoked with `executed = true` from the handler thread. If the handler is shut
        // down before the task is picked up, the task is instead invoked with `executed = false` so
        // that clients can still release whatever the task owns (the user's PixelBufferDescriptor
        // and the Vulkan objects of the request).
        using Task = utils::Invocable<void(bool executed)>;

        TaskHandler();

        void post(Task&& task);

        // This will block until all of the tasks are done.
        void drain();

        // This will quit without running the pending tasks, but they will still be invoked with
        // `executed = false` so that they can clean up after themselves.
        void shutdown();

    private:
        void loop();

        bool mShouldStop;
        utils::Condition mHasTaskCondition;
        utils::Mutex mTaskQueueMutex;
        std::queue<Task> mTaskQueue;
        std::thread mThread;
    };

    using OnReadCompleteFunction = std::function<void(PixelBufferDescriptor&&)>;

    // `onReadComplete` is called (from the handler thread) to hand the pixel buffer back to the
    // client once the readback completed - or was abandoned.
    VulkanReadPixels(VkDevice device, VulkanContext const& context,
            uint32_t graphicsQueueFamilyIndex, OnReadCompleteFunction&& onReadComplete);

    // Must be called from the backend thread.
    void terminate() noexcept;

    // Must be called from the backend thread.
    void run(fvkmemory::resource_ptr<VulkanRenderTarget> srcTarget, uint32_t x, uint32_t y,
            uint32_t width, uint32_t height, PixelBufferDescriptor&& pbd);

    // Must be called from the backend thread.
    void run(fvkmemory::resource_ptr<VulkanTexture> srcTexture, uint8_t level, uint16_t layer,
            uint32_t x, uint32_t y, uint32_t width, uint32_t height, PixelBufferDescriptor&& pbd);

    // Destroys the Vulkan objects of the requests that the handler thread has retired. Must be
    // called from the backend thread (see `Request`). This is cheap when there is nothing to
    // collect, so it can be called every tick.
    void gc();

    // This method will block until all of the in-flight requests are complete, and collects their
    // resources. Must be called from the backend thread.
    void runUntilComplete();

private:
    // The Vulkan objects backing a single readback. They are created on the backend thread and,
    // because VkCommandPool is externally synchronized, they must also be destroyed on it: the
    // handler thread hands them back via `retire()` and `gc()` destroys them.
    struct Request {
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkCommandBuffer cmdbuffer = VK_NULL_HANDLE;
    };

    // Called from the handler thread once it is done using the request's resources.
    void retire(Request const& request);

    VkDevice mDevice = VK_NULL_HANDLE;
    VulkanContext const& mContext;
    uint32_t const mGraphicsQueueFamilyIndex;
    OnReadCompleteFunction const mOnReadComplete;
    VkCommandPool mCommandPool = VK_NULL_HANDLE;
    std::unique_ptr<TaskHandler> mTaskHandler;

    utils::Mutex mRetiredRequestsMutex;
    std::vector<Request> mRetiredRequests UTILS_GUARDED_BY(mRetiredRequestsMutex);
};

}// namespace filament::backend

#endif// TNT_FILAMENT_BACKEND_VULKANREADPIXELS_H
