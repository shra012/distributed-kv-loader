/// Re-exports the Core Engine API declared in include/core_engine for import-based consumers.

module;

#include "core_engine_prelude.hpp"

export module core_engine;

export namespace core_engine {

using core_engine::Error;
using core_engine::error_code_name;
using core_engine::ErrorCode;
using core_engine::exit_code;
using core_engine::FirstFailure;
using core_engine::format_error;

using core_engine::Field;
using core_engine::FieldType;
using core_engine::Schema;

using core_engine::parse_decimal;

using core_engine::Counter;
using core_engine::NodeCounters;
using core_engine::NodeStats;

using core_engine::CancelToken;

using core_engine::BoundedQueue;
using core_engine::QueueStatus;

using core_engine::Batch;
using core_engine::BatchBuilder;

using core_engine::append_int32_field;
using core_engine::append_string_field;
using core_engine::append_u32_be;
using core_engine::append_u64_be;
using core_engine::assign_canonical_key;
using core_engine::build_canonical_key;
using core_engine::CanonicalKey;
using core_engine::FieldValue;
using core_engine::kInt32KeyTag;
using core_engine::kStringKeyTag;
using core_engine::pack_record;
using core_engine::PackedRecord;
using core_engine::read_u32_be;
using core_engine::read_u64_be;
using core_engine::RecordLayout;
using core_engine::scan_record;

using core_engine::fnv1a_64;
using core_engine::jump_consistent_hash;
using core_engine::owner_of;

using core_engine::Config;
using core_engine::load_config;

using core_engine::Command;
using core_engine::GenerateOptions;
using core_engine::HelpOptions;
using core_engine::LoadOptions;
using core_engine::parse_cli;
using core_engine::usage;
using core_engine::VerifyOptions;
using core_engine::version;
using core_engine::VersionOptions;

using core_engine::generate;
using core_engine::Manifest;
using core_engine::read_manifest;
using core_engine::write_manifest;

using core_engine::parse_file;
using core_engine::RecordCallback;
using core_engine::RecordView;

using core_engine::decode_frame;
using core_engine::DecodedFrame;
using core_engine::encode_frame;
using core_engine::encode_frame_header;
using core_engine::FrameType;
using core_engine::kFrameHeaderBytes;
using core_engine::kMaxFramePayloadBytes;
using core_engine::kMaxWireNodeId;
using core_engine::peek_payload_size;

using core_engine::MockNetwork;
using core_engine::NetworkFault;
using core_engine::NodeId;
using core_engine::ShutdownHow;
using core_engine::SocketError;

using core_engine::Store;

using core_engine::Node;

using core_engine::Cluster;
using core_engine::ClusterOptions;
using core_engine::FaultEndpoint;
using core_engine::TransportFaultInjection;

using core_engine::check_invariants;
using core_engine::format_report;
using core_engine::Invariants;
using core_engine::OwnershipVerification;
using core_engine::report_matches_manifest;
using core_engine::scan_ownership;
using core_engine::verify_report;

}
