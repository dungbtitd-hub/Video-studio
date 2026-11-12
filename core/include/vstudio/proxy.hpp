// Video Studio Core - proxy media (plan FR-017).
// Large sources (4K, high bitrate, long-GOP phone video) are transcoded in the
// background to a small, short-GOP, video-only file used only for preview.
// Export always reads the original media.
#pragma once

#include <functional>
#include <string>

#include "vstudio/media.hpp"
#include "vstudio/model.hpp"
#include "vstudio/scheduler.hpp"

namespace vstudio {

struct ProxyPolicy {
    int maxHeight = 540;               // proxy height
    std::int64_t pixelThreshold = 1920LL * 1080;  // sources above this get a proxy
};

bool needsProxy(const MediaAsset& a, const ProxyPolicy& policy = {});

struct ProxyResult {
    bool ok = false;
    std::string error;
    std::string path;
};

// Blocking; run it on the JobScheduler at Background priority.
ProxyResult generateProxy(IMediaBackend& backend, const std::string& sourcePath, const MediaAsset& asset,
                          const std::string& proxyPath, const ProxyPolicy& policy, const CancelToken& cancel,
                          const std::function<void(double)>& progress = {});

}  // namespace vstudio
