// ZeroFG glue in XenDroid's Vulkan presenter: the half of the ZeroFG host that
// lives inside the game's presenter (xenia/ui/vulkan/vulkan_presenter.cc in
// XenDroid-ZeroFG 1.0). It records a Generation on ZeroFG's device, runs
// XenDroid's normal output pipeline on a frame (the Post), and hands the
// surface over. Reference only: an excerpt, not a translation unit.
//
// The surrounding file is part of Xenia (Copyright 2022 Ben Vanik, released
// under the BSD 3-Clause license); these functions were written for ZeroFG.

struct VulkanPresenter::ZeroFGPostContext {
  struct JobContext {
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kMaxGuestOutputPaintEffects> descriptor_sets = {};
    std::array<std::unique_ptr<GuestOutputImage>,
               kMaxGuestOutputPaintEffects - 1>
        intermediate_images;
    std::array<VkFramebuffer, kMaxGuestOutputPaintEffects - 1>
        intermediate_framebuffers = {};
    std::unique_ptr<GuestOutputImage> logical_input;
    bool logical_input_ever_written = false;
    VkFramebuffer final_framebuffer = VK_NULL_HANDLE;
    VkImageView final_view = VK_NULL_HANDLE;
    // Binary semaphore whose pending signal is exported as an Android
    // sync_file, the presenter's completion authority for this Post. One
    // semaphore is tied to each physical FinalOutput job context and is reused
    // only after the presenter retires its fd.
    VkSemaphore acquire_semaphore = VK_NULL_HANDLE;
    // Completion owner of this FinalOutput slot. Its timeline exists only when
    // The effective physical profile keeps D2 per-slot completion on. Its
    // previous point belongs to the slot's previous Post, which the presenter
    // observed complete before releasing the slot, so cleanup on signal finds
    // that point retired instead of waiting on pending GPU work.
    ZeroFGCompletionOwner completion;
    uint64_t signal_value = 0;
    uint64_t submit_time_ns = 0;
    bool submitted = false;
  };

  std::array<JobContext,
             ZeroFGIndependentPresenter::kFinalOutputPoolSize>
      jobs;
  VkSemaphore completion_timeline = VK_NULL_HANDLE;
  uint64_t next_timeline_value = 1;
  // Posts signal their job's own timeline, created with the job.
  // Completion/ownership falsifiers of the Post path, reported with every
  // Post result.
  ZeroFGCompletionOwnerStats owner_stats;
  VkRenderPass final_render_pass = VK_NULL_HANDLE;
  VkFormat final_format = VK_FORMAT_UNDEFINED;
  std::array<VkPipeline, size_t(GuestOutputPaintEffect::kCount)>
      final_pipelines = {};
};

struct VulkanPresenter::ZeroFGGenerationContext {
  static constexpr uint32_t kContextCount =
      ZeroFGIndependentPresenter::kSyntheticPoolSize;
  struct CommandContext {
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    uint64_t signal_value = 0;
    uint64_t submit_time_ns = 0;
    uint64_t submit_call_ns = 0;
    bool profiling_sample_pending = false;
    bool submitted = false;
    // Completion owner of this Generation context.
    // - Timeline (effective V2-1 per-context profile): its
    //   previous point belongs to this context's previous Generation, which
    //   retired before ReleaseZeroFGSynthetic handed the context back.
    // - sync_fd (effective D3 profile): signaled by the
    //   same submit. An orphaned or dropped Generation is observed with a
    //   non-waiting poll instead of a timeline counter query, which waits on
    //   pending GPU work on Turnip/KGSL. Export failure and reuse follow the
    //   capture sync_fd (C2).
    ZeroFGCompletionOwner completion;
  };

  std::unique_ptr<zerofg::xenia::Adapter> adapter;
  std::array<CommandContext, kContextCount> commands = {};
  std::array<std::unique_ptr<GuestOutputImage>, kContextCount> outputs;
  std::array<bool, kContextCount> output_initialized = {};
  std::array<bool, kContextCount> output_busy = {};
  VkSemaphore completion_timeline = VK_NULL_HANDLE;
  VkQueryPool timestamp_query_pool = VK_NULL_HANDLE;
  uint32_t timestamp_valid_bits = 0;
  double timestamp_period_ns = 0.0;
  uint32_t timestamp_query_count_per_context = 0;
  // The adapter runs the Compat forms (what the build guard records).
  bool compat_backend = false;
  uint64_t next_timeline_value = 1;
  VkExtent2D extent = {};
  uint32_t pool_high_water = 0;
  // D3 sync_fd observation (Android only). Generations signal their command
  // context's own timeline, or completion_timeline if it failed to create.
  bool sync_fd_observation = false;
  // Completion/ownership falsifiers of the Generation path, reported with
  // every Generation result.
  ZeroFGCompletionOwnerStats owner_stats;
};

// The build guard (zerofg_build_guard.h, 1.0.1). The file includes
// "xenia/ui/vulkan/zerofg_build_guard.h" and "version.h", and declares
// DECLARE_path(storage_root).
namespace {
// What a ZeroFG pipeline build verdict belongs to: the driver, the engine whose
// shaders it compiles and the app build that shipped them.
std::string ZeroFGBuildIdentity(const VulkanDevice& device) {
  const VulkanDevice::Properties& properties = device.properties();
  return fmt::format(
      "vendor={:#x} device={:#x} driverID={} driverVersion={:#x} name={} "
      "engine=rc3 build={}",
      properties.vendorID, properties.deviceID,
      uint32_t(properties.driverID), properties.driverVersion,
      properties.deviceName, XE_BUILD_COMMIT_SHORT);
}
}  // namespace

// Excerpt of InitializeSurfaceIndependent: the verdict is read before ZeroFG's
// device is created.
//
//   // A driver that died building ZeroFG's pipelines on the Compat forms keeps
//   // ZeroFG off, and one that died on its fast paths runs Compat, until the
//   // driver or the app build changes (zerofg_build_guard.h).
//   const ZeroFGBuildGuard::Verdict build_verdict =
//       IsZeroFGRequested()
//           ? ZeroFGBuildGuard::Get().Initialize(
//                 cvars::storage_root, ZeroFGBuildIdentity(*vulkan_device_))
//           : ZeroFGBuildGuard::Verdict::kNone;
//   const bool build_guard_off =
//       build_verdict == ZeroFGBuildGuard::Verdict::kOff;
//   const bool device_context_available =
//       !build_guard_off && InitializeZeroFGDeviceContext();
//   if (device_context_available &&
//       build_verdict == ZeroFGBuildGuard::Verdict::kCompat) {
//     zerofg_vulkan_context_->backend_fallback_active = true;
//   }
//   ...
//   } else if (build_guard_off) {
//     XELOGE(
//         "ZeroFGBuildGuard: this driver crashed building ZeroFG's Compat "
//         "pipelines; ZeroFG stays off and the normal XenDroid presenter runs");
//   }

void VulkanPresenter::BeginZeroFGSurfaceDisconnect() {
  if (zerofg_vulkan_context_->handoff) {
    zerofg_vulkan_context_->handoff->DisarmAndQuiesceSource();
  }
  if (zerofg_independent_presenter_) {
    zerofg_independent_presenter_->BeginSurfaceDisconnect();
  }
}

#if XE_PLATFORM_ANDROID || XE_PLATFORM_xendroid
namespace {
// An eventfd write never blocks here; retrying EINTR means no wake is lost,
// above all the one that lets the observer exit before its join.
// Returns false on a failure other than EINTR.
}  // namespace
#endif

void VulkanPresenter::DestroyZeroFGSurfaceResourcesAfterSourceIdle() {
  // Make the ordering explicit even when the independent presenter has
  // already been detached: A's marker must be quiesced before any B teardown.
  if (zerofg_vulkan_context_->handoff) {
    zerofg_vulkan_context_->handoff->DisarmAndQuiesceSource();
  }
  guest_output_image_refresher_completion_timeline_.AwaitAllSubmissions();
  if (zerofg_independent_presenter_) {
    zerofg_independent_presenter_->DestroySurfaceResourcesAfterSourceIdle();
  }
  // The independent presenter drains B before invoking its Generation/Post
  // shutdown callbacks. Keep this fallback for a presenter that was never
  // surface-connected, and make the single physical authority obvious here.
  if (ZeroFGDevice()->is_zerofg_presenter_device() &&
      !ZeroFGDevice()->DrainZeroFGForTeardown()) {
    XELOGW("ZeroFGC0: device-B teardown drain failed before context destroy");
  }
  DestroyZeroFGGenerationContext();
  DestroyZeroFGPostContext();
  if (zerofg_vulkan_context_->handoff) {
    zerofg_vulkan_context_->handoff->DestroyAfterIdle();
  }
}

void VulkanPresenter::DestroyZeroFGPostContext() {
  if (!zerofg_vulkan_context_->post) {
    return;
  }
  ZeroFGPostContext& context = *zerofg_vulkan_context_->post;
  const VulkanDevice::Functions& dfn = ZeroFGDevice()->functions();
  const VkDevice device = ZeroFGDevice()->device();
  for (ZeroFGPostContext::JobContext& job : context.jobs) {
    util::DestroyAndNullHandle(dfn.vkDestroySemaphore, device,
                               job.acquire_semaphore);
    job.completion.DestroyTimeline(ZeroFGDevice());
    util::DestroyAndNullHandle(dfn.vkDestroyFramebuffer, device,
                               job.final_framebuffer);
    for (VkFramebuffer& framebuffer : job.intermediate_framebuffers) {
      util::DestroyAndNullHandle(dfn.vkDestroyFramebuffer, device,
                                 framebuffer);
    }
    job.logical_input.reset();
    for (auto& image : job.intermediate_images) {
      image.reset();
    }
    util::DestroyAndNullHandle(dfn.vkDestroyDescriptorPool, device,
                               job.descriptor_pool);
    util::DestroyAndNullHandle(dfn.vkDestroyCommandPool, device,
                               job.command_pool);
    job.command_buffer = VK_NULL_HANDLE;
  }
  for (VkPipeline& pipeline : context.final_pipelines) {
    util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device, pipeline);
  }
  util::DestroyAndNullHandle(dfn.vkDestroyRenderPass, device,
                             context.final_render_pass);
  util::DestroyAndNullHandle(dfn.vkDestroySemaphore, device,
                             context.completion_timeline);
  zerofg_vulkan_context_->post.reset();
}

void VulkanPresenter::DestroyZeroFGGenerationContext() {
  if (!zerofg_vulkan_context_->generation) {
    return;
  }
  ZeroFGGenerationContext& context = *zerofg_vulkan_context_->generation;
  const VulkanDevice::Functions& dfn = ZeroFGDevice()->functions();
  const VkDevice device = ZeroFGDevice()->device();
  context.outputs = {};
  context.adapter.reset();
  for (ZeroFGGenerationContext::CommandContext& command :
       context.commands) {
    util::DestroyAndNullHandle(dfn.vkDestroyCommandPool, device,
                               command.command_pool);
    command.command_buffer = VK_NULL_HANDLE;
    command.completion.DestroySyncFd(ZeroFGDevice());
    command.completion.DestroyTimeline(ZeroFGDevice());
  }
  util::DestroyAndNullHandle(dfn.vkDestroySemaphore, device,
                             context.completion_timeline);
  util::DestroyAndNullHandle(dfn.vkDestroyQueryPool, device,
                             context.timestamp_query_pool);
  zerofg_vulkan_context_->generation.reset();
}

void VulkanPresenter::ReleaseZeroFGSynthetic(uint32_t synthetic_index) {
  if (!zerofg_vulkan_context_->generation ||
      synthetic_index >= ZeroFGGenerationContext::kContextCount) {
    return;
  }
  ZeroFGGenerationContext& context = *zerofg_vulkan_context_->generation;
  ZeroFGGenerationContext::CommandContext& command =
      context.commands[synthetic_index];
  command.submitted = false;
  // The Generation has retired (its Post completed, or a poll observed it).
  // Rule 5 falsifier: while this Generation still holds its own sync_fd,
  // check that claim without waiting before the context is handed back. A
  // pending fd means the next signal of this context's timeline could wait
  // on it.
  if (command.completion.occupied() &&
      command.completion.sync_fd_observable()) {
    ++context.owner_stats.release_checks;
    const ZeroFGCompletionOwner::SyncFdPoll poll_state =
        command.completion.PollSyncFd();
    if (poll_state == ZeroFGCompletionOwner::SyncFdPoll::kPending) {
      ++context.owner_stats.release_unretired;
    } else if (poll_state == ZeroFGCompletionOwner::SyncFdPoll::kError) {
      ++context.owner_stats.sync_fd_errors;
      command.completion.AbandonSyncFd();
    }
  }
  // The sync_fd is no longer needed. A pending recreation is kept: a
  // semaphore whose export failed still holds its signal and is replaced
  // before this context's next Generation submit.
  command.completion.RetireSyncFd();
  command.completion.MarkRetired();
  context.output_busy[synthetic_index] = false;
}

bool VulkanPresenter::PrepareZeroFGGenerationContext(
    ZeroFGIndependentPresenter::GenerationResult& result,
    uint64_t& setup_ns, bool& created_out) {
  using FailureStage =
      ZeroFGIndependentPresenter::GenerationFailureStage;
  created_out = false;
  if (zerofg_vulkan_context_->generation) {
    return true;
  }

  const uint64_t setup_begin_ns = GetZeroFGMonotonicTimeNs();
  const VulkanDevice::Functions& dfn = ZeroFGDevice()->functions();
  const VkDevice device = ZeroFGDevice()->device();
  const uint32_t queue_family = ZeroFGDevice()->queue_family_graphics_compute();
  auto context = std::make_unique<ZeroFGGenerationContext>();
  const VulkanInstance* instance = ZeroFGDevice()->vulkan_instance();
  const bool backend_fallback = zerofg_vulkan_context_->backend_fallback_active;
  if (backend_fallback) {
    XELOGW("ZeroFGBackendFallback active=true: this generation context runs "
           "the Compat backend");
  }
  zerofg::VulkanContext vulkan = {};
  vulkan.instance = instance->instance();
  vulkan.physical_device = ZeroFGDevice()->physical_device();
  vulkan.device = device;
  vulkan.get_instance_proc_addr = instance->functions().vkGetInstanceProcAddr;
  zerofg::Status adapter_status = zerofg::Status::kSuccess;
  zerofg::Capabilities capabilities =
      ZeroFGDevice()->zerofg_backend_capabilities();
  capabilities.effective_api_version = ZeroFGDevice()->properties().apiVersion;
  capabilities.synchronization2_enabled =
      ZeroFGDevice()->properties().synchronization2;
  capabilities.shader_storage_image_extended_formats_enabled =
      ZeroFGDevice()->properties().shaderStorageImageExtendedFormats;
  VkSemaphoreTypeCreateInfo timeline_type = {
      VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
  timeline_type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  VkSemaphoreCreateInfo semaphore_info = {
      VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  semaphore_info.pNext = &timeline_type;
  const VkResult timeline_result = dfn.vkCreateSemaphore(
      device, &semaphore_info, nullptr, &context->completion_timeline);
  if (timeline_result != VK_SUCCESS) {
    result.failure_stage = FailureStage::kAdapter;
    result.vk_result = timeline_result;
    result.vk_result_valid = true;
    setup_ns += GetZeroFGMonotonicTimeNs() - setup_begin_ns;
    return false;
  }

  const VulkanDevice::QueueFamily& queue_family_properties =
      ZeroFGDevice()->queue_families()[queue_family];
  context->timestamp_valid_bits =
      queue_family_properties.timestamp_valid_bits;
  context->timestamp_period_ns =
      double(ZeroFGDevice()->properties().timestampPeriod);
  if (context->timestamp_valid_bits && context->timestamp_period_ns > 0.0) {
    context->timestamp_query_count_per_context = kZeroFGTimestampCount;
    VkQueryPoolCreateInfo query_info = {
        VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    query_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    query_info.queryCount = ZeroFGGenerationContext::kContextCount *
                            context->timestamp_query_count_per_context;
    if (dfn.vkCreateQueryPool(device, &query_info, nullptr,
                              &context->timestamp_query_pool) != VK_SUCCESS) {
      context->timestamp_query_pool = VK_NULL_HANDLE;
      context->timestamp_valid_bits = 0;
      context->timestamp_query_count_per_context = 0;
    }
  }
  // Auto: the generic fast paths the device really has (Modern); a failure
  // retries once on Compat (ZeroFGBackendFallback).
  context->compat_backend = backend_fallback;
  {
    const ZeroFGBuildGuard::Scope build =
        ZeroFGBuildGuard::Get().Build("adapter", context->compat_backend);
    context->adapter = zerofg::xenia::Adapter::Create(
        vulkan, ZeroFGGenerationContext::kContextCount,
        IsReallyZeroRequested() ? zerofg::Mode::kReallyZero
                                : zerofg::Mode::kZero,
        backend_fallback ? zerofg::Backend::kCompat : zerofg::Backend::kAuto,
        capabilities, &adapter_status);
  }
  if (!context->adapter) {
    util::DestroyAndNullHandle(dfn.vkDestroyQueryPool, device,
                               context->timestamp_query_pool);
    util::DestroyAndNullHandle(dfn.vkDestroySemaphore, device,
                               context->completion_timeline);
    if (!backend_fallback) {
      XELOGW("ZeroFGBackendFallback stage=adapter status={}: retrying the "
             "generation context on the Compat backend",
             uint32_t(adapter_status));
      zerofg_vulkan_context_->backend_fallback_active = true;
      context.reset();
      return PrepareZeroFGGenerationContext(result, setup_ns, created_out);
    }
    result.failure_stage = FailureStage::kAdapter;
    result.zerofg_status = uint32_t(adapter_status);
    setup_ns += GetZeroFGMonotonicTimeNs() - setup_begin_ns;
    return false;
  }
  XELOGI("ZeroFGVariant mode={} backend={} timestamp_valid_bits={} "
         "timestamp_period_ns={}",
         ZeroFGSelectionName(), backend_fallback ? "compat" : "auto",
         context->timestamp_valid_bits, context->timestamp_period_ns);

  context->sync_fd_observation = kZeroFGSyncFdSupported;
  for (auto& command : context->commands) {
    VkCommandPoolCreateInfo pool_info = {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
                      VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = queue_family;
    const VkResult pool_result = dfn.vkCreateCommandPool(
        device, &pool_info, nullptr, &command.command_pool);
    if (pool_result != VK_SUCCESS) {
      result.failure_stage = FailureStage::kAdapter;
      result.vk_result = pool_result;
      result.vk_result_valid = true;
      zerofg_vulkan_context_->generation = std::move(context);
      DestroyZeroFGGenerationContext();
      setup_ns += GetZeroFGMonotonicTimeNs() - setup_begin_ns;
      return false;
    }
    VkCommandBufferAllocateInfo allocate = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = command.command_pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    const VkResult allocate_result = dfn.vkAllocateCommandBuffers(
        device, &allocate, &command.command_buffer);
    if (allocate_result != VK_SUCCESS) {
      result.failure_stage = FailureStage::kAdapter;
      result.vk_result = allocate_result;
      result.vk_result_valid = true;
      zerofg_vulkan_context_->generation = std::move(context);
      DestroyZeroFGGenerationContext();
      setup_ns += GetZeroFGMonotonicTimeNs() - setup_begin_ns;
      return false;
    }
    if (context->sync_fd_observation &&
        command.completion.CreateSyncFd(
            ZeroFGDevice(), "ZeroFG Generation sync_fd") != VK_SUCCESS) {
      // ZeroFG needs the per-command sync_fd semaphore.
      command.completion.SetSyncFdFallback();
      zerofg_vulkan_context_->generation = std::move(context);
      DestroyZeroFGGenerationContext();
      result.failure_stage = FailureStage::kAdapter;
      return false;
    }
    if (command.completion.CreateTimeline(
            ZeroFGDevice(),
            "ZeroFG Generation completion " +
                std::to_string(&command - context->commands.data())) !=
        VK_SUCCESS) {
      ++context->owner_stats.timeline_create_failures;
      zerofg_vulkan_context_->generation = std::move(context);
      DestroyZeroFGGenerationContext();
      result.failure_stage = FailureStage::kAdapter;
      return false;
    }
  }

  zerofg_vulkan_context_->generation = std::move(context);
  setup_ns += GetZeroFGMonotonicTimeNs() - setup_begin_ns;
  created_out = true;
  return true;
}

bool VulkanPresenter::ReadZeroFGPreviousProfilingSample(
    uint32_t synthetic_index,
    ZeroFGIndependentPresenter::GenerationResult& result) {
  if (!zerofg_vulkan_context_->generation ||
      synthetic_index >= ZeroFGGenerationContext::kContextCount) {
    return false;
  }
  ZeroFGGenerationContext& context = *zerofg_vulkan_context_->generation;
  ZeroFGGenerationContext::CommandContext& command =
      context.commands[synthetic_index];
  if (!command.profiling_sample_pending) {
    return true;
  }

  ++result.profile_readback_attempts;
  // The slot is being reused only after its previous submission completed.
  // Read without WAIT and consume the sample before the new query range is
  // reset. A lost/invalid profile must never affect product scheduling.
  command.profiling_sample_pending = false;
  if (context.timestamp_query_pool == VK_NULL_HANDLE ||
      !context.timestamp_query_count_per_context) {
    ++result.profile_readback_errors;
    result.profile_readback_last_error = VK_ERROR_INITIALIZATION_FAILED;
    return false;
  }

  const uint32_t timestamp_count = context.timestamp_query_count_per_context;
  std::array<uint64_t, kZeroFGTimestampCount> timestamps = {};
  const VkResult query_result =
      ZeroFGDevice()->functions().vkGetQueryPoolResults(
          ZeroFGDevice()->device(), context.timestamp_query_pool,
          synthetic_index * timestamp_count, timestamp_count,
          sizeof(uint64_t) * timestamp_count, timestamps.data(),
          sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
  if (query_result == VK_NOT_READY) {
    ++result.profile_readback_not_ready;
    return false;
  }
  if (query_result != VK_SUCCESS) {
    ++result.profile_readback_errors;
    result.profile_readback_last_error = query_result;
    return false;
  }

  ++result.profile_readback_successes;
  result.drained_profile_sample_valid = true;
  auto& sample = result.drained_profile_sample;
  sample.submit_call_ns = command.submit_call_ns;

  const auto delta_ticks = [&context](uint64_t begin,
                                      uint64_t end) -> uint64_t {
    if (context.timestamp_valid_bits < 64) {
      const uint64_t mask =
          (uint64_t(1) << context.timestamp_valid_bits) - 1;
      return (end - begin) & mask;
    }
    return end - begin;
  };
  sample.service_gpu_ns = uint64_t(
      double(delta_ticks(timestamps[0], timestamps[1])) *
      context.timestamp_period_ns);
  sample.timestamp_valid = sample.service_gpu_ns != 0;
  return true;
}

bool VulkanPresenter::ProcessZeroFGGeneration(
    const ZeroFGIndependentPresenter::GenerationRequest& request,
    ZeroFGIndependentPresenter::GenerationResult& result) {
  using FailureStage =
      ZeroFGIndependentPresenter::GenerationFailureStage;
  result = {};
  const uint64_t request_begin_ns = GetZeroFGMonotonicTimeNs();
  uint64_t setup_ns = 0;
  const auto finish_failure = [&result, &setup_ns, request_begin_ns]() {
    result.setup_ns = setup_ns;
    result.total_ns = GetZeroFGMonotonicTimeNs() - request_begin_ns;
  };
  const auto fail = [&result, &finish_failure](FailureStage stage) {
    result.failure_stage = stage;
    finish_failure();
    return false;
  };
  const auto fail_status = [&result, &finish_failure](
                               FailureStage stage, zerofg::Status status) {
    result.failure_stage = stage;
    result.zerofg_status = uint32_t(status);
    finish_failure();
    return false;
  };
  const auto fail_vk = [this, &result, &finish_failure](FailureStage stage,
                                                        VkResult vk_result) {
    if (vk_result == VK_ERROR_DEVICE_LOST) {
      ZeroFGDevice()->SetLost();
      XELOGE(
          "ZeroFGDeviceB lost operation=Generation stage={} "
          "Source_A_untouched=true",
          uint32_t(stage));
    }
    result.failure_stage = stage;
    result.vk_result = vk_result;
    result.vk_result_valid = true;
    finish_failure();
    return false;
  };
  const auto input_waits_valid = [&request]() {
    if (!request.input_wait_count ||
        request.input_wait_count > request.input_wait_semaphores.size()) {
      return false;
    }
    for (uint32_t i = 0; i < request.input_wait_count; ++i) {
      if (request.input_wait_semaphores[i] == VK_NULL_HANDLE ||
          !request.input_wait_values[i]) {
        return false;
      }
    }
    // Each semaphore appears once: the presenter folds A/B Residency images
    // on the same timeline into one wait with the higher value.
    return request.input_wait_count < 2 ||
           request.input_wait_semaphores[0] !=
               request.input_wait_semaphores[1];
  };
  if (request.previous_image == VK_NULL_HANDLE ||
      request.previous_view == VK_NULL_HANDLE ||
      request.current_image == VK_NULL_HANDLE ||
      request.current_view == VK_NULL_HANDLE || !request.extent.width ||
      !request.extent.height || !request.source_physical_extent.width ||
      !request.source_physical_extent.height ||
      request.source_physical_extent.width < request.extent.width ||
      request.source_physical_extent.height < request.extent.height ||
      !input_waits_valid() ||
      !request.previous_source_id ||
      // Accepted adjacency forms the pair; an IssueSwap gap is valid.
      request.current_source_id <= request.previous_source_id) {
    return fail(FailureStage::kRequestValidation);
  }

  const VulkanDevice::Functions& dfn = ZeroFGDevice()->functions();
  const VkDevice device = ZeroFGDevice()->device();
  const uint32_t queue_family = ZeroFGDevice()->queue_family_graphics_compute();
  const uint32_t queue_index = ZeroFGDevice()->queue_index_zerofg_presenter();
  bool generation_context_created = false;
  if (!PrepareZeroFGGenerationContext(result, setup_ns,
                                         generation_context_created)) {
    finish_failure();
    return false;
  }
  if (generation_context_created) {
    // Pipeline compilation is first-use-heavy. If prewarming was
    // unavailable, yield after creating the context rather than combining it
    // with Resize, command recording and queue acquisition in one arbiter
    // quantum.
    return fail(FailureStage::kPreparationYield);
  }

  ZeroFGGenerationContext& context = *zerofg_vulkan_context_->generation;
  if (context.extent.width != request.extent.width ||
      context.extent.height != request.extent.height) {
    const uint64_t setup_begin_ns = GetZeroFGMonotonicTimeNs();
    if (std::any_of(context.output_busy.begin(), context.output_busy.end(),
                    [](bool busy) { return busy; })) {
      return fail(FailureStage::kResize);
    }
    zerofg::Status resize_status = zerofg::Status::kSuccess;
    {
      // RC3 builds its pipelines here, at the first Generation.
      const ZeroFGBuildGuard::Scope build =
          ZeroFGBuildGuard::Get().Build("resize", context.compat_backend);
      resize_status = context.adapter->Resize(
          request.extent.width, request.extent.height, kGuestOutputFormat,
          kGuestOutputFormat);
    }
    XELOGI("ZeroFGGeneration resize picture={}x{} status={}",
           request.extent.width, request.extent.height,
           uint32_t(resize_status));
    if (resize_status != zerofg::Status::kSuccess) {
      if (resize_status == zerofg::Status::kVulkanError &&
          !zerofg_vulkan_context_->backend_fallback_active) {
        // A Modern fast path must never take generation down: drop the
        // context and let the next cycle recreate it on Compat.
        XELOGW("ZeroFGBackendFallback stage=resize status={}: recreating the "
               "generation context on the Compat backend",
               uint32_t(resize_status));
        zerofg_vulkan_context_->backend_fallback_active = true;
        DestroyZeroFGGenerationContext();
        return fail(FailureStage::kPreparationYield);
      }
      return fail_status(FailureStage::kResize, resize_status);
    }
    context.outputs = {};
    context.output_initialized = {};
    for (auto& output : context.outputs) {
      output = GuestOutputImage::Create(ZeroFGDevice(), request.extent.width,
                                        request.extent.height,
                                        VK_IMAGE_USAGE_TRANSFER_DST_BIT);
      if (!output) {
        context.outputs = {};
        return fail(FailureStage::kResize);
      }
    }
    context.extent = request.extent;
    setup_ns += GetZeroFGMonotonicTimeNs() - setup_begin_ns;
    // Return Source-critical authority between first-use allocation and the
    // first Generation submission. The same logical retries next cycle.
    return fail(FailureStage::kPreparationYield);
  }

  uint32_t selected = UINT32_MAX;
  for (uint32_t i = 0; i < ZeroFGGenerationContext::kContextCount; ++i) {
    if (!context.output_busy[i]) {
      context.output_busy[i] = true;
      selected = i;
      break;
    }
  }
  if (selected == UINT32_MAX) {
    return fail(FailureStage::kSyntheticPool);
  }
  const auto release_before_submit = [&context, selected]() {
    context.output_busy[selected] = false;
  };
  uint32_t occupancy = 0;
  for (bool busy : context.output_busy) {
    occupancy += busy;
  }
  context.pool_high_water = std::max(context.pool_high_water, occupancy);
  result.pool_occupancy = occupancy;
  result.pool_high_water = context.pool_high_water;

  ZeroFGGenerationContext::CommandContext& command =
      context.commands[selected];
  // Drain the previous generation's profiling queries before resetting this
  // slot for the new command buffer. This is a non-blocking observation only;
  // the slot-reuse contract already proves the prior submission completed.
  ReadZeroFGPreviousProfilingSample(selected, result);
  VkResult vk_result =
      dfn.vkResetCommandPool(device, command.command_pool, 0);
  if (vk_result != VK_SUCCESS) {
    release_before_submit();
    return fail_vk(FailureStage::kCommandReset, vk_result);
  }
  VkCommandBufferBeginInfo begin = {
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vk_result = dfn.vkBeginCommandBuffer(command.command_buffer, &begin);
  if (vk_result != VK_SUCCESS) {
    release_before_submit();
    return fail_vk(FailureStage::kCommandBegin, vk_result);
  }

  VkImageMemoryBarrier real_acquires[2] = {};
  const VkImageLayout real_layouts[2] = {request.previous_layout,
                                         request.current_layout};
  const VkImage real_images[2] = {request.previous_image,
                                  request.current_image};
  for (uint32_t i = 0; i < xe::countof(real_acquires); ++i) {
    VkImageMemoryBarrier& barrier = real_acquires[i];
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.oldLayout = real_layouts[i];
    barrier.newLayout = real_layouts[i];
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.image = real_images[i];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
  }
  dfn.vkCmdPipelineBarrier(command.command_buffer,
                           VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                           0, nullptr, uint32_t(xe::countof(real_acquires)),
                           real_acquires);

  if (!context.output_initialized[selected]) {
    VkImageMemoryBarrier initialize = {
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    initialize.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    initialize.newLayout = kGuestOutputInternalLayout;
    initialize.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    initialize.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    initialize.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    initialize.image = context.outputs[selected]->image();
    initialize.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    initialize.subresourceRange.levelCount = 1;
    initialize.subresourceRange.layerCount = 1;
    dfn.vkCmdPipelineBarrier(command.command_buffer,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &initialize);
  }

  // `sequence` is the accepted-Real identity (the source id): RC3 uses the
  // previous pair's motion only when this pair starts at the Real the
  // previous pair ended on. 0 = unknown (no temporal prior); other modes
  // ignore it.
  const auto wrap_real = [&request](VkImage image, VkImageView view,
                                    VkImageLayout layout,
                                    uint64_t sequence) {
    zerofg::Image wrapped = {};
    wrapped.image = image;
    wrapped.view = view;
    wrapped.layout = layout;
    wrapped.sequence = sequence;
    wrapped.format = kGuestOutputFormat;
    wrapped.width = request.source_physical_extent.width;
    wrapped.height = request.source_physical_extent.height;
    wrapped.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT;
    // AllocateResidencySlot creates optimal, flags=0, identity 2D views.
    wrapped.creation_facts_known = true;
    wrapped.active_rect = {0, 0, request.extent.width, request.extent.height};
    return wrapped;
  };
  const auto wrap_internal = [&request](const GuestOutputImage& image) {
    zerofg::Image wrapped = {};
    wrapped.image = image.image();
    wrapped.view = image.view();
    wrapped.layout = kGuestOutputInternalLayout;
    wrapped.format = kGuestOutputFormat;
    wrapped.width = request.extent.width;
    wrapped.height = request.extent.height;
    wrapped.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT |
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_STORAGE_BIT;
    wrapped.creation_facts_known = true;
    wrapped.active_rect = {0, 0, request.extent.width, request.extent.height};
    return wrapped;
  };
  const uint32_t timestamp_query =
      selected * context.timestamp_query_count_per_context;
  if (context.timestamp_query_pool != VK_NULL_HANDLE) {
    dfn.vkCmdResetQueryPool(command.command_buffer,
                            context.timestamp_query_pool, timestamp_query,
                            context.timestamp_query_count_per_context);
    // The pair around the Generation: its GPU service time.
    dfn.vkCmdWriteTimestamp(command.command_buffer,
                            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                            context.timestamp_query_pool, timestamp_query);
  }
  const zerofg::Status interpolate_status = context.adapter->Interpolate(
      command.command_buffer, selected,
      wrap_real(request.previous_image, request.previous_view,
                request.previous_layout, request.previous_source_id),
      wrap_real(request.current_image, request.current_view,
                request.current_layout, request.current_source_id),
      0.5f,
      wrap_internal(*context.outputs[selected]));
  if (interpolate_status != zerofg::Status::kSuccess) {
    dfn.vkResetCommandPool(device, command.command_pool, 0);
    release_before_submit();
    return fail_status(FailureStage::kRecord, interpolate_status);
  }
  if (context.timestamp_query_pool != VK_NULL_HANDLE) {
    dfn.vkCmdWriteTimestamp(command.command_buffer,
                            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                            context.timestamp_query_pool,
                            timestamp_query + 1);
  }

  VkImageMemoryBarrier real_releases[2] = {};
  for (uint32_t i = 0; i < xe::countof(real_releases); ++i) {
    VkImageMemoryBarrier& barrier = real_releases[i];
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = real_layouts[i];
    barrier.newLayout = real_layouts[i];
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = real_images[i];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
  }
  dfn.vkCmdPipelineBarrier(command.command_buffer,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr,
                           0, nullptr, uint32_t(xe::countof(real_releases)),
                           real_releases);
  vk_result = dfn.vkEndCommandBuffer(command.command_buffer);
  if (vk_result != VK_SUCCESS) {
    release_before_submit();
    return fail_vk(FailureStage::kCommandEnd, vk_result);
  }

  // V2-1: signal this context's own timeline. Its previous point belongs to
  // this context's previous Generation, which retired before the context was
  // released, so the signal's cleanup never waits on another context's
  // pending Generation. Without it, the shared Generation timeline.
  const bool completion_per_context = command.completion.owns_timeline();
  const VkSemaphore generation_timeline = completion_per_context
                                              ? command.completion.timeline()
                                              : context.completion_timeline;
  const uint64_t signal_value = completion_per_context
                                    ? command.completion.ClaimTimelineValue()
                                    : context.next_timeline_value++;
  if (command.completion.occupied()) {
    ++context.owner_stats.reuse_before_retire;
  }
  // D3: a sync semaphore whose export failed on an earlier use still holds
  // that signal. This context was released, so its previous Generation has
  // retired; replace the semaphore before signaling it again.
  if (command.completion.sync_fd_recreate_pending()) {
    command.completion.RecreateSyncFd(ZeroFGDevice(),
                                      "ZeroFG Generation sync_fd");
  }
  const bool signal_sync_fd =
      context.sync_fd_observation && command.completion.can_signal_sync_fd();
  const uint32_t signal_semaphore_count = signal_sync_fd ? 2 : 1;
  const VkSemaphore signal_semaphores[2] = {
      generation_timeline, command.completion.sync_semaphore()};
  // One value per signal semaphore; the binary sync_fd semaphore's is ignored.
  const uint64_t signal_values[2] = {signal_value, 0};
  // V2-2: one input wait per readiness timeline. The presenter folds the A/B
  // Residency images into one wait when they share a timeline and passes two
  // when each Residency slot owns its readiness timeline.
  const VkPipelineStageFlags input_wait_stages[2] = {
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT};
  VkTimelineSemaphoreSubmitInfo timeline_submit = {
      VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
  timeline_submit.waitSemaphoreValueCount = request.input_wait_count;
  timeline_submit.pWaitSemaphoreValues = request.input_wait_values.data();
  timeline_submit.signalSemaphoreValueCount = signal_semaphore_count;
  timeline_submit.pSignalSemaphoreValues = signal_values;
  VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.pNext = &timeline_submit;
  submit.waitSemaphoreCount = request.input_wait_count;
  submit.pWaitSemaphores = request.input_wait_semaphores.data();
  submit.pWaitDstStageMask = input_wait_stages;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &command.command_buffer;
  submit.signalSemaphoreCount = signal_semaphore_count;
  submit.pSignalSemaphores = signal_semaphores;
  const uint64_t queue_request_ns = GetZeroFGMonotonicTimeNs();
  uint64_t submit_begin_ns = 0;
  {
    std::optional<VulkanDevice::Queue::Acquisition> queue =
        ZeroFGDevice()->TryAcquireQueue(queue_family, queue_index);
    while (!queue && request.queue_commit_allowed &&
           request.queue_commit_allowed()) {
      std::this_thread::yield();
      queue = ZeroFGDevice()->TryAcquireQueue(queue_family, queue_index);
    }
    if (queue && request.queue_commit_allowed &&
        !request.queue_commit_allowed()) {
      queue.reset();
    }
    if (!queue) {
      result.queue_wait_ns = GetZeroFGMonotonicTimeNs() - queue_request_ns;
      // Physical Source runway or lifecycle state revoked this S's q0
      // authority. Keep it pending and return to the arbiter without submit.
      dfn.vkResetCommandPool(device, command.command_pool, 0);
      release_before_submit();
      return fail(FailureStage::kQueueBusy);
    }
    if (ZeroFGDevice()->RejectZeroFGSubmitAfterTeardownIdle()) {
      dfn.vkResetCommandPool(device, command.command_pool, 0);
      release_before_submit();
      return fail(FailureStage::kQueueBusy);
    }
    submit_begin_ns = GetZeroFGMonotonicTimeNs();
    result.queue_wait_ns = submit_begin_ns - queue_request_ns;
    vk_result =
        dfn.vkQueueSubmit(queue->queue(), 1, &submit, VK_NULL_HANDLE);
    result.submit_host_ns = GetZeroFGMonotonicTimeNs() - submit_begin_ns;
  }
  if (vk_result != VK_SUCCESS) {
    release_before_submit();
    return fail_vk(FailureStage::kQueueSubmit, vk_result);
  }
  result.submission_accepted = true;
  command.signal_value = signal_value;
  command.submit_time_ns = GetZeroFGMonotonicTimeNs();
  command.submitted = true;
  command.completion.MarkSubmitted(generation_timeline);
  context.owner_stats.CountSubmit(completion_per_context,
                                  result.submit_host_ns);
  // D3: export the sync_fd after queue acceptance, outside the queue lock.
  // Without an fd this Generation falls back to its completion timeline if
  // it ever needs CPU observation.
  command.completion.CloseSyncFd();
  if (signal_sync_fd) {
    const uint64_t export_begin_ns = GetZeroFGMonotonicTimeNs();
    const bool exported = command.completion.ExportSyncFd(ZeroFGDevice());
    result.completion_fd_export_attempted = true;
    result.completion_fd_export_host_ns =
        GetZeroFGMonotonicTimeNs() - export_begin_ns;
    result.completion_fd_exported = exported;
    result.completion_fd_fallback = !exported;
  } else if (context.sync_fd_observation) {
    command.completion.SetSyncFdFallback();
    result.completion_fd_fallback = true;
  }
  context.output_initialized[selected] = true;
  result.image = context.outputs[selected]->image();
  result.view = context.outputs[selected]->view();
  result.extent = context.extent;
  result.synthetic_index = selected;
  result.signal_value = signal_value;
  result.completion_semaphore = generation_timeline;
  result.completion_per_context = completion_per_context;
  result.completion_owner_stats = context.owner_stats;
  result.submit_time_ns = command.submit_time_ns;
  command.submit_call_ns = result.submit_host_ns;
  command.profiling_sample_pending =
      context.timestamp_query_pool != VK_NULL_HANDLE;
  result.setup_ns = setup_ns;
  result.total_ns = GetZeroFGMonotonicTimeNs() - request_begin_ns;
  return true;
}

bool VulkanPresenter::PollZeroFGGeneration(
    uint32_t synthetic_index,
    ZeroFGIndependentPresenter::GenerationResult& result,
    bool& ready_out) {
  using FailureStage =
      ZeroFGIndependentPresenter::GenerationFailureStage;
  result = {};
  ready_out = false;
  if (!zerofg_vulkan_context_->generation ||
      synthetic_index >= ZeroFGGenerationContext::kContextCount) {
    result.failure_stage = FailureStage::kRequestValidation;
    return false;
  }
  ZeroFGGenerationContext& context = *zerofg_vulkan_context_->generation;
  ZeroFGGenerationContext::CommandContext& command =
      context.commands[synthetic_index];
  if (!command.submitted || !command.signal_value) {
    result.failure_stage = FailureStage::kRequestValidation;
    return false;
  }
  // D3: observe an orphaned or dropped Generation through its sync_fd with a
  // non-waiting poll. The counter query on the timeline this Generation
  // signaled (the shared one, or its context's own with V2-1) stays as the
  // fallback when no fd was exported; on Turnip/KGSL it waits on pending GPU
  // work.
  bool complete = false;
  bool observed = false;
  if (ZeroFGDevice()->zerofg_teardown_idle()) {
    complete = observed = true;
  } else if (command.completion.sync_fd_observable()) {
    const uint64_t poll_begin_ns = GetZeroFGMonotonicTimeNs();
    const ZeroFGCompletionOwner::SyncFdPoll poll_state =
        command.completion.PollSyncFd();
    result.completion_fd_poll_host_ns =
        GetZeroFGMonotonicTimeNs() - poll_begin_ns;
    if (poll_state == ZeroFGCompletionOwner::SyncFdPoll::kError) {
      // The fd proves nothing any more; the timeline decides below.
      ++context.owner_stats.sync_fd_errors;
      command.completion.AbandonSyncFd();
    } else {
      result.completion_fd_polled = true;
      observed = true;
      complete = poll_state == ZeroFGCompletionOwner::SyncFdPoll::kSignaled;
    }
  }
  if (!observed) {
    result.failure_stage = FailureStage::kTimelineQuery;
    result.submission_accepted = true;
    return false;  // Fail-open drain owns retirement, never query pending B.
  }
  result.completion_owner_stats = context.owner_stats;
  if (!complete) {
    return true;
  }
  command.completion.CloseSyncFd();
  command.completion.MarkRetired();
  ready_out = true;
  result.submission_accepted = true;
  result.signal_value = command.signal_value;
  result.submit_time_ns = command.submit_time_ns;
  result.submit_to_ready_ns =
      GetZeroFGMonotonicTimeNs() - command.submit_time_ns;
  result.gpu_completion_ns = result.submit_to_ready_ns;
  result.image = context.outputs[synthetic_index]->image();
  result.view = context.outputs[synthetic_index]->view();
  result.extent = context.extent;
  result.synthetic_index = synthetic_index;
  // Profiling queries are drained by ProcessZeroFGGeneration when this slot
  // is reused. Polling here must remain completion/liveness-only: the normal
  // Generation->Post GPU chain deliberately does not require a CPU readback.
  command.submitted = false;
  return true;
}

bool VulkanPresenter::ProcessZeroFGPost(
    const ZeroFGIndependentPresenter::PostProcessRequest& request,
    ZeroFGIndependentPresenter::PostProcessResult& result) {
  using PostFailureStage =
      ZeroFGIndependentPresenter::PostFailureStage;
  result = {};
  const auto fail = [&result](PostFailureStage stage) {
    result.failure_stage = stage;
    return false;
  };
  const auto fail_vk = [this, &result](PostFailureStage stage,
                                       VkResult vk_result) {
    if (vk_result == VK_ERROR_DEVICE_LOST) {
      ZeroFGDevice()->SetLost();
      XELOGE(
          "ZeroFGDeviceB lost operation=Post stage={} Source_A_untouched=true",
          uint32_t(stage));
    }
    result.failure_stage = stage;
    result.vk_result = vk_result;
    result.vk_result_valid = true;
    return false;
  };
  const auto handle_value = [](auto handle) -> uint64_t {
    using Handle = decltype(handle);
    if constexpr (std::is_pointer_v<Handle>) {
      return uint64_t(reinterpret_cast<uintptr_t>(handle));
    } else {
      return uint64_t(handle);
    }
  };
  if (!request.frontbuffer_width || !request.frontbuffer_height ||
      !request.display_aspect_ratio_x || !request.display_aspect_ratio_y ||
      request.candidate_image == VK_NULL_HANDLE ||
      request.candidate_layout == VK_IMAGE_LAYOUT_UNDEFINED ||
      request.candidate_storage_extent.width < request.frontbuffer_width ||
      request.candidate_storage_extent.height < request.frontbuffer_height ||
      request.final_output_image == VK_NULL_HANDLE ||
      request.final_output_view == VK_NULL_HANDLE ||
      !request.final_output_extent.width ||
      !request.final_output_extent.height ||
      request.candidate_image == request.final_output_image ||
      request.final_output_index >=
          ZeroFGIndependentPresenter::kFinalOutputPoolSize ||
      ((request.candidate_wait_semaphore == VK_NULL_HANDLE) !=
       (request.candidate_wait_value == 0)) ||
      request.final_output_format == VK_FORMAT_UNDEFINED) {
    XELOGE(
        "ZeroFGC0PostInvariant kind={} source={} pair_a={} logical={} "
        "candidate=0x{:X} storage={}x{} logical_extent={}x{} "
        "final=0x{:X} final_extent={}x{} reason=request_contract",
        request.candidate_is_synthetic ? "S" : "R", request.source_id,
        request.pair_a_source_id, request.logical_sequence,
        handle_value(request.candidate_image),
        request.candidate_storage_extent.width,
        request.candidate_storage_extent.height, request.frontbuffer_width,
        request.frontbuffer_height, handle_value(request.final_output_image),
        request.final_output_extent.width, request.final_output_extent.height);
    return fail(PostFailureStage::kRequestValidation);
  }
  const VulkanDevice::Functions& dfn = ZeroFGDevice()->functions();
  const VkDevice device = ZeroFGDevice()->device();
  const uint32_t queue_family = ZeroFGDevice()->queue_family_graphics_compute();
  const uint32_t queue_index = ZeroFGDevice()->queue_index_zerofg_presenter();
  if (!zerofg_vulkan_context_->post) {
    auto context = std::make_unique<ZeroFGPostContext>();
    VkCommandPoolCreateInfo pool_info = {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_family;
    VkSemaphoreTypeCreateInfo timeline_type = {
        VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    timeline_type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo semaphore_info = {
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    semaphore_info.pNext = &timeline_type;
    const VkResult timeline_result = dfn.vkCreateSemaphore(
        device, &semaphore_info, nullptr, &context->completion_timeline);
    if (timeline_result != VK_SUCCESS) {
      zerofg_vulkan_context_->post = std::move(context);
      DestroyZeroFGPostContext();
      return fail_vk(PostFailureStage::kContextTimeline, timeline_result);
    }
    // The shared layout contains both a sampled image and an immutable
    // sampler. Vulkan still accounts immutable samplers against the descriptor
    // pool on drivers that enforce this strictly, so mirror the normal
    // presenter pool instead of declaring sampled images only.
    VkDescriptorPoolSize descriptor_pool_sizes[2];
    descriptor_pool_sizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    descriptor_pool_sizes[0].descriptorCount = kMaxGuestOutputPaintEffects;
    descriptor_pool_sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    descriptor_pool_sizes[1].descriptorCount = kMaxGuestOutputPaintEffects;
    VkDescriptorPoolCreateInfo descriptor_pool_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptor_pool_info.maxSets = kMaxGuestOutputPaintEffects;
    descriptor_pool_info.poolSizeCount = uint32_t(
        xe::countof(descriptor_pool_sizes));
    descriptor_pool_info.pPoolSizes = descriptor_pool_sizes;
    std::array<VkDescriptorSetLayout, kMaxGuestOutputPaintEffects> layouts;
    layouts.fill(zerofg_vulkan_context_->image_layout);
    for (ZeroFGPostContext::JobContext& job : context->jobs) {
      const VkResult command_pool_result = dfn.vkCreateCommandPool(
          device, &pool_info, nullptr, &job.command_pool);
      if (command_pool_result != VK_SUCCESS) {
        zerofg_vulkan_context_->post = std::move(context);
        DestroyZeroFGPostContext();
        return fail_vk(PostFailureStage::kContextCommandPool,
                       command_pool_result);
      }
      VkCommandBufferAllocateInfo command_info = {
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
      command_info.commandPool = job.command_pool;
      command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      command_info.commandBufferCount = 1;
      const VkResult command_buffer_result = dfn.vkAllocateCommandBuffers(
          device, &command_info, &job.command_buffer);
      if (command_buffer_result != VK_SUCCESS) {
        zerofg_vulkan_context_->post = std::move(context);
        DestroyZeroFGPostContext();
        return fail_vk(PostFailureStage::kContextCommandBuffer,
                       command_buffer_result);
      }
      const VkResult descriptor_pool_result = dfn.vkCreateDescriptorPool(
          device, &descriptor_pool_info, nullptr, &job.descriptor_pool);
      if (descriptor_pool_result != VK_SUCCESS) {
        zerofg_vulkan_context_->post = std::move(context);
        DestroyZeroFGPostContext();
        return fail_vk(PostFailureStage::kDescriptorPool,
                       descriptor_pool_result);
      }
      VkDescriptorSetAllocateInfo descriptor_allocate = {
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
      descriptor_allocate.descriptorPool = job.descriptor_pool;
      descriptor_allocate.descriptorSetCount = uint32_t(layouts.size());
      descriptor_allocate.pSetLayouts = layouts.data();
      const VkResult descriptor_sets_result = dfn.vkAllocateDescriptorSets(
          device, &descriptor_allocate, job.descriptor_sets.data());
      if (descriptor_sets_result != VK_SUCCESS) {
        zerofg_vulkan_context_->post = std::move(context);
        DestroyZeroFGPostContext();
        return fail_vk(PostFailureStage::kDescriptorSets,
                       descriptor_sets_result);
      }
      VkExportSemaphoreCreateInfo export_info = {
          VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
      export_info.handleTypes =
          VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
      VkSemaphoreCreateInfo acquire_info = {
          VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
      acquire_info.pNext = &export_info;
      const VkResult acquire_result = dfn.vkCreateSemaphore(
          device, &acquire_info, nullptr, &job.acquire_semaphore);
      if (acquire_result != VK_SUCCESS) {
        zerofg_vulkan_context_->post = std::move(context);
        DestroyZeroFGPostContext();
        return fail_vk(PostFailureStage::kContextTimeline, acquire_result);
      }
      const VkResult job_timeline_result = job.completion.CreateTimeline(
          ZeroFGDevice(), "ZeroFG Post completion " +
                              std::to_string(&job - context->jobs.data()));
      if (job_timeline_result != VK_SUCCESS) {
        zerofg_vulkan_context_->post = std::move(context);
        DestroyZeroFGPostContext();
        return fail_vk(PostFailureStage::kContextTimeline,
                       job_timeline_result);
      }
    }
    VkAttachmentDescription attachment = {};
    attachment.format = request.final_output_format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference attachment_ref = {};
    attachment_ref.attachment = 0;
    attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &attachment_ref;
    VkRenderPassCreateInfo render_pass_info = {
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    const VkResult render_pass_result = dfn.vkCreateRenderPass(
        device, &render_pass_info, nullptr, &context->final_render_pass);
    if (render_pass_result != VK_SUCCESS) {
      zerofg_vulkan_context_->post = std::move(context);
      DestroyZeroFGPostContext();
      return fail_vk(PostFailureStage::kFinalRenderPass, render_pass_result);
    }
    context->final_format = request.final_output_format;
    zerofg_vulkan_context_->post = std::move(context);
  }
  ZeroFGPostContext& context = *zerofg_vulkan_context_->post;
  if (context.final_format != request.final_output_format) {
    return fail(PostFailureStage::kFinalFormat);
  }
  ZeroFGPostContext::JobContext& job =
      context.jobs[request.final_output_index];
  if (job.submitted) {
    return fail(PostFailureStage::kCommandReset);
  }

  const VkExtent2D logical_extent = {
      request.frontbuffer_width, request.frontbuffer_height};
  if (!job.logical_input ||
      job.logical_input->extent().width != logical_extent.width ||
      job.logical_input->extent().height != logical_extent.height) {
    job.logical_input = GuestOutputImage::Create(
        ZeroFGDevice(), logical_extent.width, logical_extent.height,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    job.logical_input_ever_written = false;
    if (!job.logical_input) {
      return fail(PostFailureStage::kLogicalInput);
    }
    VkDescriptorImageInfo image_descriptor = {};
    image_descriptor.imageView = job.logical_input->view();
    image_descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = job.descriptor_sets[0];
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &image_descriptor;
    dfn.vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
  }

  VkExtent2D max_extent =
      util::GetMax2DFramebufferExtent(ZeroFGDevice()->properties());
  GuestOutputProperties properties;
  properties.frontbuffer_width = request.frontbuffer_width;
  properties.frontbuffer_height = request.frontbuffer_height;
  properties.display_aspect_ratio_x = request.display_aspect_ratio_x;
  properties.display_aspect_ratio_y = request.display_aspect_ratio_y;
  properties.is_8bpc = request.is_8bpc;
  GuestOutputPaintFlow flow = GetGuestOutputPaintFlow(
      properties, request.final_output_extent.width,
      request.final_output_extent.height, max_extent.width, max_extent.height,
      request.config);
  if (!flow.effect_count) {
    return fail(PostFailureStage::kPaintFlow);
  }
  const std::pair<uint32_t, uint32_t>& final_effect_size =
      flow.effect_output_sizes[flow.effect_count - 1];
  const int64_t final_right =
      int64_t(flow.output_x) + int64_t(final_effect_size.first);
  const int64_t final_bottom =
      int64_t(flow.output_y) + int64_t(final_effect_size.second);
  if (!final_effect_size.first || !final_effect_size.second ||
      final_right <= 0 || final_bottom <= 0 ||
      flow.output_x >= int32_t(request.final_output_extent.width) ||
      flow.output_y >= int32_t(request.final_output_extent.height)) {
    XELOGE(
        "ZeroFGC0PostInvariant kind={} source={} pair_a={} logical={} "
        "final_rect={},{},{}x{} final_extent={}x{} reason=paint_flow_rect",
        request.candidate_is_synthetic ? "S" : "R", request.source_id,
        request.pair_a_source_id, request.logical_sequence, flow.output_x,
        flow.output_y, final_effect_size.first, final_effect_size.second,
        request.final_output_extent.width, request.final_output_extent.height);
    return fail(PostFailureStage::kPaintFlow);
  }
  result.effect_count = uint32_t(flow.effect_count);

  for (size_t i = 0; i < kMaxGuestOutputPaintEffects - 1; ++i) {
    std::pair<uint32_t, uint32_t> needed = {};
    if (i + 1 < flow.effect_count) {
      needed = flow.effect_output_sizes[i];
    }
    const VkExtent2D current = job.intermediate_images[i]
                                   ? job.intermediate_images[i]->extent()
                                   : VkExtent2D{};
    if (current.width == needed.first && current.height == needed.second) {
      continue;
    }
    job.intermediate_images[i].reset();
    util::DestroyAndNullHandle(dfn.vkDestroyFramebuffer, device,
                               job.intermediate_framebuffers[i]);
    if (!needed.first || !needed.second) {
      continue;
    }
    job.intermediate_images[i] =
        GuestOutputImage::Create(ZeroFGDevice(), needed.first, needed.second);
    if (!job.intermediate_images[i]) {
      return fail(PostFailureStage::kIntermediateResource);
    }
    VkImageView attachment = job.intermediate_images[i]->view();
    VkFramebufferCreateInfo framebuffer_info = {
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer_info.renderPass = zerofg_vulkan_context_->render_pass;
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.pAttachments = &attachment;
    framebuffer_info.width = needed.first;
    framebuffer_info.height = needed.second;
    framebuffer_info.layers = 1;
    const VkResult framebuffer_result = dfn.vkCreateFramebuffer(
        device, &framebuffer_info, nullptr,
        &job.intermediate_framebuffers[i]);
    if (framebuffer_result != VK_SUCCESS) {
      return fail_vk(PostFailureStage::kIntermediateFramebuffer,
                     framebuffer_result);
    }
    VkDescriptorImageInfo image_descriptor = {};
    image_descriptor.imageView = job.intermediate_images[i]->view();
    image_descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = job.descriptor_sets[i + 1];
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &image_descriptor;
    dfn.vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
  }
  for (size_t i = 0; i + 1 < flow.effect_count; ++i) {
    const size_t effect_index = size_t(flow.effects[i]);
    if (zerofg_vulkan_context_->fragments[effect_index] == VK_NULL_HANDLE ||
        zerofg_vulkan_context_->pipelines[effect_index] == VK_NULL_HANDLE) {
      return fail(PostFailureStage::kIntermediatePipeline);
    }
  }
  const GuestOutputPaintEffect final_effect =
      flow.effects[flow.effect_count - 1];
  if (zerofg_vulkan_context_->fragments[size_t(final_effect)] ==
      VK_NULL_HANDLE) {
    return fail(PostFailureStage::kFinalPipeline);
  }
  VkPipeline& final_pipeline = context.final_pipelines[size_t(final_effect)];
  if (final_pipeline == VK_NULL_HANDLE) {
    VkResult pipeline_result = VK_SUCCESS;
    final_pipeline = CreateGuestOutputPaintPipeline(
        final_effect, context.final_render_pass, &pipeline_result, true);
    if (final_pipeline == VK_NULL_HANDLE) {
      return fail_vk(PostFailureStage::kFinalPipeline, pipeline_result);
    }
  }
  if (job.final_view != request.final_output_view) {
    util::DestroyAndNullHandle(
        dfn.vkDestroyFramebuffer, device,
        job.final_framebuffer);
    VkFramebufferCreateInfo framebuffer_info = {
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer_info.renderPass = context.final_render_pass;
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.pAttachments = &request.final_output_view;
    framebuffer_info.width = request.final_output_extent.width;
    framebuffer_info.height = request.final_output_extent.height;
    framebuffer_info.layers = 1;
    const VkResult framebuffer_result = dfn.vkCreateFramebuffer(
        device, &framebuffer_info, nullptr,
        &job.final_framebuffer);
    if (framebuffer_result != VK_SUCCESS) {
      return fail_vk(PostFailureStage::kFinalFramebuffer, framebuffer_result);
    }
    job.final_view = request.final_output_view;
  }
  const VkResult reset_result =
      dfn.vkResetCommandPool(device, job.command_pool, 0);
  if (reset_result != VK_SUCCESS) {
    return fail_vk(PostFailureStage::kCommandReset, reset_result);
  }
  VkCommandBufferBeginInfo begin = {
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  const VkResult begin_result =
      dfn.vkBeginCommandBuffer(job.command_buffer, &begin);
  if (begin_result != VK_SUCCESS) {
    return fail_vk(PostFailureStage::kCommandBegin, begin_result);
  }
  VkImageMemoryBarrier acquire_barriers[3] = {};
  for (VkImageMemoryBarrier& barrier : acquire_barriers) {
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
  }
  acquire_barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  acquire_barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  acquire_barriers[0].oldLayout = request.candidate_layout;
  acquire_barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  acquire_barriers[0].srcAccessMask =
      VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  acquire_barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  acquire_barriers[0].image = request.candidate_image;
  acquire_barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  acquire_barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  acquire_barriers[1].oldLayout = job.logical_input_ever_written
                                      ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                      : VK_IMAGE_LAYOUT_UNDEFINED;
  acquire_barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  acquire_barriers[1].srcAccessMask = job.logical_input_ever_written
                                         ? VK_ACCESS_SHADER_READ_BIT
                                         : 0;
  acquire_barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  acquire_barriers[1].image = job.logical_input->image();
  // Main Surface Authority: a B-local FinalOutput never leaves device B, and
  // the Post clears all of it, so its previous contents (the egress copy's
  // source, retired before the slot came back) are simply discarded.
  acquire_barriers[2].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  acquire_barriers[2].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  acquire_barriers[2].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  acquire_barriers[2].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  acquire_barriers[2].srcAccessMask = 0;
  acquire_barriers[2].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  acquire_barriers[2].image = request.final_output_image;
  dfn.vkCmdPipelineBarrier(
      job.command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      0, 0, nullptr, 0, nullptr, uint32_t(xe::countof(acquire_barriers)),
      acquire_barriers);
  VkImageCopy copy = {};
  copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copy.srcSubresource.layerCount = 1;
  copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copy.dstSubresource.layerCount = 1;
  copy.extent = {logical_extent.width, logical_extent.height, 1};
  dfn.vkCmdCopyImage(job.command_buffer, request.candidate_image,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     job.logical_input->image(),
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
  VkImageMemoryBarrier input_ready = {
      VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  input_ready.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  input_ready.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  input_ready.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  input_ready.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  input_ready.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  input_ready.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  input_ready.image = job.logical_input->image();
  input_ready.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  input_ready.subresourceRange.levelCount = 1;
  input_ready.subresourceRange.layerCount = 1;
  dfn.vkCmdPipelineBarrier(job.command_buffer,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                           0, nullptr, 1, &input_ready);

  VkViewport viewport = {};
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  VkRect2D scissor = {};
  VkClearValue black = {};
  VkClearAttachment clear_attachment = {};
  clear_attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  clear_attachment.colorAttachment = 0;
  for (size_t i = 0; i < flow.effect_count; ++i) {
    const bool final = i + 1 == flow.effect_count;
    const auto effect_size = flow.effect_output_sizes[i];
    int32_t rect_x = final ? flow.output_x : 0;
    int32_t rect_y = final ? flow.output_y : 0;
    VkRenderPassBeginInfo render_begin = {
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    render_begin.renderPass = final ? context.final_render_pass
                                    : zerofg_vulkan_context_->render_pass;
    render_begin.framebuffer =
        final ? job.final_framebuffer : job.intermediate_framebuffers[i];
    render_begin.renderArea.extent =
        final ? request.final_output_extent
              : VkExtent2D{effect_size.first, effect_size.second};
    if (final) {
      render_begin.clearValueCount = 1;
      render_begin.pClearValues = &black;
    }
    dfn.vkCmdBeginRenderPass(job.command_buffer, &render_begin,
                             VK_SUBPASS_CONTENTS_INLINE);
    viewport.width = float(render_begin.renderArea.extent.width);
    viewport.height = float(render_begin.renderArea.extent.height);
    scissor.extent = render_begin.renderArea.extent;
    dfn.vkCmdSetViewport(job.command_buffer, 0, 1, &viewport);
    dfn.vkCmdSetScissor(job.command_buffer, 0, 1, &scissor);
    const GuestOutputPaintEffect effect = flow.effects[i];
    const VkPipeline pipeline =
        final ? context.final_pipelines[size_t(effect)]
              : zerofg_vulkan_context_->pipelines[size_t(effect)];
    dfn.vkCmdBindPipeline(job.command_buffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const GuestOutputPaintPipelineLayoutIndex layout_index =
        GetGuestOutputPaintPipelineLayoutIndex(effect);
    const VkPipelineLayout layout =
        zerofg_vulkan_context_->layouts[layout_index];
    // Set 0 samples the exact-size logical input; set i samples intermediate
    // i-1 for every later effect.
    const VkDescriptorSet descriptor = job.descriptor_sets[i];
    dfn.vkCmdBindDescriptorSets(job.command_buffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
                                &descriptor, 0, nullptr);
    GuestOutputPaintRectangleConstants rectangle;
    const float x_to_ndc = 2.0f / viewport.width;
    const float y_to_ndc = 2.0f / viewport.height;
    rectangle.x = -1.0f + float(rect_x) * x_to_ndc;
    rectangle.y = -1.0f + float(rect_y) * y_to_ndc;
    rectangle.width = float(effect_size.first) * x_to_ndc;
    rectangle.height = float(effect_size.second) * y_to_ndc;
    dfn.vkCmdPushConstants(job.command_buffer, layout,
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(rectangle),
                           &rectangle);
    uint32_t constants_size = 0;
    union {
      BilinearConstants bilinear;
      CasSharpenConstants cas_sharpen;
      CasResampleConstants cas_resample;
      FsrEasuConstants fsr_easu;
      FsrRcasConstants fsr_rcas;
      SgsrConstants sgsr;
    } constants;
    switch (layout_index) {
      case kGuestOutputPaintPipelineLayoutIndexBilinear:
        constants_size = sizeof(constants.bilinear);
        constants.bilinear.Initialize(flow, i);
        break;
      case kGuestOutputPaintPipelineLayoutIndexCasSharpen:
        constants_size = sizeof(constants.cas_sharpen);
        constants.cas_sharpen.Initialize(flow, i, request.config);
        break;
      case kGuestOutputPaintPipelineLayoutIndexCasResample:
        constants_size = sizeof(constants.cas_resample);
        constants.cas_resample.Initialize(flow, i, request.config);
        break;
      case kGuestOutputPaintPipelineLayoutIndexFsrEasu:
        constants_size = sizeof(constants.fsr_easu);
        constants.fsr_easu.Initialize(flow, i);
        break;
      case kGuestOutputPaintPipelineLayoutIndexFsrRcas:
        constants_size = sizeof(constants.fsr_rcas);
        constants.fsr_rcas.Initialize(flow, i, request.config);
        break;
      case kGuestOutputPaintPipelineLayoutIndexSgsr:
        constants_size = sizeof(constants.sgsr);
        constants.sgsr.Initialize(flow, i);
        break;
      default:
        break;
    }
    if (constants_size) {
      dfn.vkCmdPushConstants(job.command_buffer, layout,
                             VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(rectangle),
                             constants_size, &constants);
    }
    dfn.vkCmdDraw(job.command_buffer, 4, 1, 0, 0);
    if (final && flow.letterbox_clear_rectangle_count) {
      std::array<VkClearRect, GuestOutputPaintFlow::kMaxClearRectangles> clears;
      for (size_t clear_index = 0;
           clear_index < flow.letterbox_clear_rectangle_count; ++clear_index) {
        const auto& source = flow.letterbox_clear_rectangles[clear_index];
        VkClearRect& clear = clears[clear_index];
        clear.rect.offset = {int32_t(source.x), int32_t(source.y)};
        clear.rect.extent = {source.width, source.height};
        clear.baseArrayLayer = 0;
        clear.layerCount = 1;
      }
      dfn.vkCmdClearAttachments(
          job.command_buffer, 1, &clear_attachment,
          uint32_t(flow.letterbox_clear_rectangle_count), clears.data());
    }
    dfn.vkCmdEndRenderPass(job.command_buffer);
  }
  VkImageMemoryBarrier releases[2] = {};
  for (VkImageMemoryBarrier& barrier : releases) {
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
  }
  releases[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  releases[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  releases[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  releases[0].newLayout = request.candidate_layout;
  releases[0].image = request.candidate_image;
  releases[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  releases[1].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  releases[1].image = request.final_output_image;
  // Main Surface Authority: the FinalOutput stays on device B, ready for the
  // egress copy (same queue family, so no ownership transfer). The copy's wait
  // on this Post's timeline point makes the writes visible to it.
  releases[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  releases[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  releases[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  dfn.vkCmdPipelineBarrier(job.command_buffer,
                           VK_PIPELINE_STAGE_TRANSFER_BIT |
                               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr,
                           0, nullptr, uint32_t(xe::countof(releases)), releases);
  const VkResult end_result = dfn.vkEndCommandBuffer(job.command_buffer);
  if (end_result != VK_SUCCESS) {
    return fail_vk(PostFailureStage::kCommandEnd, end_result);
  }
  // D2: with per-slot completion the Post signals its own slot's timeline, so
  // the signal's cleanup never waits on another slot's pending Post.
  const bool completion_per_slot = job.completion.owns_timeline();
  const VkSemaphore post_completion_timeline =
      completion_per_slot ? job.completion.timeline()
                          : context.completion_timeline;
  const uint64_t signal_value = completion_per_slot
                                    ? job.completion.ClaimTimelineValue()
                                    : context.next_timeline_value++;
  if (job.completion.occupied()) {
    ++context.owner_stats.reuse_before_retire;
  }
  const uint64_t wait_values[1] = {request.candidate_wait_value};
  const uint64_t signal_values[2] = {signal_value, 0};
  VkTimelineSemaphoreSubmitInfo timeline_submit = {
      VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
  timeline_submit.waitSemaphoreValueCount =
      request.candidate_wait_semaphore != VK_NULL_HANDLE ? 1 : 0;
  timeline_submit.pWaitSemaphoreValues = wait_values;
  timeline_submit.signalSemaphoreValueCount = 2;
  timeline_submit.pSignalSemaphoreValues = signal_values;
  // The candidate's final Generation barrier and layout are part of the
  // dependency, not merely its first transfer read. Keep the chained Post
  // wholly behind the timeline value even if command recording evolves.
  const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  const VkSemaphore signal_semaphores[2] = {post_completion_timeline,
                                            job.acquire_semaphore};
  VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.pNext = &timeline_submit;
  submit.waitSemaphoreCount =
      request.candidate_wait_semaphore != VK_NULL_HANDLE ? 1 : 0;
  submit.pWaitSemaphores = &request.candidate_wait_semaphore;
  submit.pWaitDstStageMask = &wait_stage;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &job.command_buffer;
  submit.signalSemaphoreCount = 2;
  submit.pSignalSemaphores = signal_semaphores;
  // Telemetry only: whether the candidate dependency is known complete when
  // the Post enters q0. With a pending wait it stays unknown: the counter
  // query that could tell waits on pending GPU work on Turnip/KGSL.
  result.input_has_wait = request.candidate_wait_semaphore != VK_NULL_HANDLE;
  if (!result.input_has_wait) {
    result.input_readiness_known = true;
    result.input_complete_at_submit = true;
  }
  const uint64_t queue_request_ns = GetZeroFGMonotonicTimeNs();
  result.submit_request_ns = queue_request_ns;
  VkResult submit_result;
  uint64_t submit_begin_ns;
  {
    const VulkanDevice::Queue::Acquisition queue =
        ZeroFGDevice()->AcquireQueue(queue_family, queue_index);
    if (ZeroFGDevice()->RejectZeroFGSubmitAfterTeardownIdle()) {
      dfn.vkResetCommandPool(device, job.command_pool, 0);
      return fail(PostFailureStage::kQueueSubmit);
    }
    submit_begin_ns = GetZeroFGMonotonicTimeNs();
    result.queue_wait_ns = submit_begin_ns - queue_request_ns;
    submit_result = dfn.vkQueueSubmit(queue.queue(), 1, &submit, VK_NULL_HANDLE);
    result.submit_host_ns = GetZeroFGMonotonicTimeNs() - submit_begin_ns;
  }
  if (submit_result != VK_SUCCESS) {
    return fail_vk(PostFailureStage::kQueueSubmit, submit_result);
  }
  result.submission_accepted = true;
  job.signal_value = signal_value;
  job.submit_time_ns = GetZeroFGMonotonicTimeNs();
  job.submitted = true;
  job.logical_input_ever_written = true;
  job.completion.MarkSubmitted(post_completion_timeline);
  context.owner_stats.CountSubmit(completion_per_slot, result.submit_host_ns);
  result.signal_value = signal_value;
  result.completion_semaphore = post_completion_timeline;
  result.completion_per_slot = completion_per_slot;
  result.completion_owner_stats = context.owner_stats;
  result.submit_time_ns = job.submit_time_ns;
  VkSemaphoreGetFdInfoKHR fd_info = {
      VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};
  fd_info.semaphore = job.acquire_semaphore;
  fd_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
  const uint64_t export_begin_ns = GetZeroFGMonotonicTimeNs();
  const VkResult export_result = ZeroFGDevice()->vkGetSemaphoreFdKHR()(
      device, &fd_info, &result.acquire_fence_fd);
  result.export_host_ns = GetZeroFGMonotonicTimeNs() - export_begin_ns;
  if (export_result != VK_SUCCESS) {
    return fail_vk(PostFailureStage::kAcquireFenceExport, export_result);
  }
  result.acquire_fence_exported = true;
  return true;
}

void VulkanPresenter::ReleaseZeroFGPost(uint32_t final_output_index) {
  if (!zerofg_vulkan_context_->post ||
      final_output_index >= ZeroFGIndependentPresenter::kFinalOutputPoolSize) {
    return;
  }
  ZeroFGPostContext::JobContext& job =
      zerofg_vulkan_context_->post->jobs[final_output_index];
  job.submitted = false;
  job.signal_value = 0;
  job.submit_time_ns = 0;
  // The presenter observed this Post complete before releasing the slot.
  job.completion.MarkRetired();
}
