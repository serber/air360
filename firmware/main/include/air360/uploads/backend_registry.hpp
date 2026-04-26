#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "air360/uploads/backend_config.hpp"
#include "air360/uploads/backend_uploader.hpp"

namespace air360 {

using BackendValidationFn  = bool (*)(const BackendRecord& record, std::string& error);
using BackendUploaderFactory = std::unique_ptr<IBackendUploader> (*)();

struct BackendTypeDefaults {
    const char*     host;
    const char*     path;
    std::uint16_t   port;
    BackendProtocol protocol;
    bool            host_is_fixed;
    bool            path_is_fixed;
};

struct BackendDescriptor {
    BackendType          type          = BackendType::kUnknown;
    const char*          backend_key   = nullptr;
    const char*          display_name  = nullptr;
    BackendTypeDefaults  defaults      = {};
    BackendValidationFn  validate      = nullptr;
    BackendUploaderFactory create_uploader = nullptr;
};

class BackendRegistry {
  public:
    const BackendDescriptor* descriptors() const;
    std::size_t descriptorCount() const;
    const BackendDescriptor* findByType(BackendType type) const;
    const BackendDescriptor* findByKey(const std::string& backend_key) const;
    bool validateRecord(const BackendRecord& record, std::string& error) const;
};

}  // namespace air360
