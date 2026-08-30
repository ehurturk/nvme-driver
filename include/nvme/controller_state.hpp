#pragma once

#include <cstdint>
#include <vector>

namespace nvme {

struct NamespaceInfo {
    std::uint32_t nsid;
    std::uint64_t size;
    std::uint64_t capacity;
    std::uint64_t in_use;
    std::uint32_t lba_size_bytes;
    std::uint16_t metadata_size;
};

class ControllerState {
public:
    ControllerState() = default;

    ControllerState(
        std::uint8_t max_data_transfer_size,
        std::uint16_t controller_id,
        std::uint8_t submission_queue_entry_size,
        std::uint8_t completion_queue_entry_size,
        std::uint32_t no_namespaces
    )
        : max_data_transfer_size_{max_data_transfer_size},
          controller_id_{controller_id},
          submission_queue_entry_size_{submission_queue_entry_size},
          completion_queue_entry_size_{completion_queue_entry_size},
          no_namespaces_{no_namespaces} {
    }

    std::uint8_t max_data_transfer_size() const {
        return max_data_transfer_size_;
    }

    std::uint16_t controller_id() const {
        return controller_id_;
    }

    std::uint8_t submission_queue_entry_size() const {
        return submission_queue_entry_size_;
    }

    std::uint8_t completion_queue_entry_size() const {
        return completion_queue_entry_size_;
    }

    std::uint32_t no_namespaces() const {
        return no_namespaces_;
    }

    void add_namespace(NamespaceInfo info) {
        namespaces_.push_back(info);
    }

    const std::vector<NamespaceInfo>& namespaces() const {
        return namespaces_;
    }

private:
    std::uint8_t max_data_transfer_size_{};
    std::uint16_t controller_id_{};
    std::uint8_t submission_queue_entry_size_{};
    std::uint8_t completion_queue_entry_size_{};
    std::uint32_t no_namespaces_{};

    std::vector<NamespaceInfo> namespaces_;
};

}  // namespace nvme
