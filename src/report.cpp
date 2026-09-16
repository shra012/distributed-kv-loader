/// Implements post-join invariant checking and the deterministic job report grammar.

#include "core_engine/report.hpp"

#include "core_engine/decimal.hpp"
#include "core_engine/error.hpp"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace core_engine {
namespace {

std::uint64_t sum(const std::vector<NodeStats>& stats, const std::uint64_t NodeStats::* field) {
  std::uint64_t total = 0U;
  for (const NodeStats& entry : stats) {
    total += entry.*field;
  }
  return total;
}

[[noreturn]] void report_error(ErrorCode code, std::string detail,
                               const std::filesystem::path& path) {
  throw Error(code, std::move(detail), std::nullopt, path.string());
}

std::string field_value(const std::string& line, std::string_view name,
                        const std::filesystem::path& path) {
  const std::string marker = " " + std::string(name) + "=";
  const std::size_t position = line.find(marker);
  if (position == std::string::npos ||
      line.find(marker, position + marker.size()) != std::string::npos) {
    report_error(ErrorCode::Verification, "malformed report field", path);
  }
  const std::size_t begin = position + marker.size();
  const std::size_t end = line.find(' ', begin);
  return line.substr(begin, end == std::string::npos ? end : end - begin);
}

std::uint64_t report_number(const std::string& line, std::string_view name,
                            const std::filesystem::path& path) {
  const std::optional<std::uint64_t> value = parse_decimal(field_value(line, name, path));
  if (!value.has_value()) {
    report_error(ErrorCode::Verification, "invalid report number", path);
  }
  return *value;
}

struct SavedReport final {
  std::uint64_t version{0U};
  std::uint64_t nodes{0U};
  std::uint64_t files{0U};
  std::uint64_t lines{0U};
  std::uint64_t parsed{0U};
  std::uint64_t unique{0U};
  std::uint64_t duplicates{0U};
  std::vector<std::uint64_t> loaded;
  std::size_t ownership_lines{0U};
  bool job{false};
  bool summary{false};
  bool timing{false};
};

SavedReport read_saved_report(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    report_error(ErrorCode::InputFile, "cannot open report file", path);
  }
  SavedReport report;
  std::string line;
  while (std::getline(input, line)) {
    if (line.rfind("JOB ", 0U) == 0U) {
      if (report.job || field_value(line, "status", path) != "SUCCESS") {
        report_error(ErrorCode::Verification, "invalid report JOB line", path);
      }
      report.version = report_number(line, "version", path);
      report.nodes = report_number(line, "nodes", path);
      report.files = report_number(line, "files", path);
      report.lines = report_number(line, "lines", path);
      report.parsed = report_number(line, "parsed", path);
      report.unique = report_number(line, "unique", path);
      report.duplicates = report_number(line, "duplicates", path);
      report.job = true;
    } else if (line.rfind("NODE ", 0U) == 0U) {
      const std::uint64_t id = report_number(line, "id", path);
      if (id != report.loaded.size()) {
        report_error(ErrorCode::Verification, "non-contiguous report node IDs", path);
      }
      report.loaded.push_back(report_number(line, "loaded", path));
    } else if (line.rfind("VERIFY node=", 0U) == 0U) {
      if (field_value(line, "result", path) != "PASS" ||
          report_number(line, "misowned", path) != 0U) {
        report_error(ErrorCode::Verification, "report ownership verification failed", path);
      }
      ++report.ownership_lines;
    } else if (line.rfind("VERIFY ownership=", 0U) == 0U) {
      if (field_value(line, "ownership", path) != "PASS" ||
          field_value(line, "uniqueness", path) != "PASS" ||
          field_value(line, "accounting", path) != "PASS" ||
          field_value(line, "completion", path) != "PASS") {
        report_error(ErrorCode::Verification, "report invariant verification failed", path);
      }
      report.summary = true;
    } else if (line.rfind("EXPECT ", 0U) == 0U) {
      if (field_value(line, "manifest", path) != "PASS") {
        report_error(ErrorCode::ManifestMismatch, "saved report records a manifest mismatch", path);
      }
    } else if (line.rfind("TIMING ", 0U) == 0U) {
      report.timing = true;
    } else if (line.rfind("DIST ", 0U) != 0U) {
      report_error(ErrorCode::Verification, "unknown report line", path);
    }
  }
  if (input.bad()) {
    report_error(ErrorCode::InputFile, "failed while reading report file", path);
  }
  if (!report.job || !report.summary || !report.timing || report.nodes != report.loaded.size() ||
      report.ownership_lines != report.loaded.size() || report.lines != report.parsed ||
      report.lines < report.unique || report.lines - report.unique != report.duplicates) {
    report_error(ErrorCode::Verification, "incomplete or inconsistent report", path);
  }
  return report;
}

}

bool report_matches_manifest(const Config& config, const Manifest& manifest,
                             const std::vector<NodeStats>& stats) {
  std::uint32_t files = 0U;
  for (const auto& path : config.input_paths()) {
    if (path.has_value()) {
      ++files;
    }
  }
  const std::uint64_t total_lines = sum(stats, &NodeStats::lines_read);
  const std::uint64_t total_unique = sum(stats, &NodeStats::loaded);
  const std::uint64_t total_duplicates = sum(stats, &NodeStats::duplicates);
  if (manifest.files != files || manifest.expected_per_node.size() != config.node_count() ||
      manifest.lines != total_lines || manifest.unique != total_unique ||
      manifest.duplicates != total_duplicates ||
      manifest.expected_per_node.size() != stats.size()) {
    return false;
  }
  for (std::size_t node = 0U; node < stats.size(); ++node) {
    if (manifest.expected_per_node[node] != stats[node].loaded) {
      return false;
    }
  }
  return true;
}

std::vector<OwnershipVerification> scan_ownership(const Cluster& cluster, const Config& config) {
  std::vector<OwnershipVerification> results;
  results.reserve(config.node_count());
  for (std::uint32_t node = 0U; node < config.node_count(); ++node) {
    const Store::OwnershipScanResult scan = cluster.store(node).scan_ownership();
    results.push_back(OwnershipVerification{node, scan.scanned, scan.misowned});
  }
  return results;
}

Invariants check_invariants(const Config& config, const std::vector<NodeStats>& node_stats,
                            const std::vector<OwnershipVerification>& scans) {
  bool ownership = true;
  for (const OwnershipVerification& scan : scans) {
    if (scan.misowned != 0U) {
      ownership = false;
    }
  }

  bool accounting = true;
  for (const NodeStats& entry : node_stats) {
    if (entry.parsed != entry.local + entry.remote_out) {
      accounting = false;
    }
    if (entry.ingested != entry.loaded + entry.duplicates) {
      accounting = false;
    }
  }
  if (sum(node_stats, &NodeStats::remote_out) != sum(node_stats, &NodeStats::remote_in)) {
    accounting = false;
  }
  if (sum(node_stats, &NodeStats::frames_out) != sum(node_stats, &NodeStats::frames_in)) {
    accounting = false;
  }
  if (sum(node_stats, &NodeStats::bytes_out) != sum(node_stats, &NodeStats::bytes_in)) {
    accounting = false;
  }
  if (sum(node_stats, &NodeStats::parsed) != sum(node_stats, &NodeStats::ingested)) {
    accounting = false;
  }

  bool completion = true;
  for (const NodeStats& entry : node_stats) {
    if (entry.end_markers_received != config.node_count()) {
      completion = false;
    }
  }

  return Invariants{ownership, ownership, accounting, completion};
}

std::string format_report(const Config& config, const std::vector<NodeStats>& node_stats,
                          const std::vector<OwnershipVerification>& scans,
                          const Invariants& invariants, const std::optional<Manifest>& manifest,
                          std::chrono::milliseconds elapsed, std::uint64_t input_bytes_total) {
  std::uint32_t files = 0U;
  for (const auto& path : config.input_paths()) {
    if (path.has_value()) {
      ++files;
    }
  }
  const std::uint64_t total_lines = sum(node_stats, &NodeStats::lines_read);
  const std::uint64_t total_parsed = sum(node_stats, &NodeStats::parsed);
  const std::uint64_t total_unique = sum(node_stats, &NodeStats::loaded);
  const std::uint64_t total_duplicates = sum(node_stats, &NodeStats::duplicates);

  std::ostringstream out;
  out << "JOB status=SUCCESS version=" << config.version() << " nodes=" << config.node_count()
      << " files=" << files << " lines=" << total_lines << " parsed=" << total_parsed
      << " unique=" << total_unique << " duplicates=" << total_duplicates << '\n';

  for (std::size_t node = 0U; node < node_stats.size(); ++node) {
    const NodeStats& entry = node_stats[node];
    out << "NODE id=" << node << " parsed=" << entry.parsed << " local=" << entry.local
        << " remote_out=" << entry.remote_out << " remote_in=" << entry.remote_in
        << " ingested=" << entry.ingested << " loaded=" << entry.loaded
        << " duplicates=" << entry.duplicates << " frames_out=" << entry.frames_out
        << " frames_in=" << entry.frames_in << " bytes_out=" << entry.bytes_out
        << " bytes_in=" << entry.bytes_in << '\n';
  }

  double mean = 0.0;
  std::uint64_t minimum = node_stats.empty() ? 0U : node_stats.front().loaded;
  std::uint64_t maximum = 0U;
  for (const NodeStats& entry : node_stats) {
    mean += static_cast<double>(entry.loaded);
    minimum = entry.loaded < minimum ? entry.loaded : minimum;
    maximum = entry.loaded > maximum ? entry.loaded : maximum;
  }
  mean = node_stats.empty() ? 0.0 : mean / static_cast<double>(node_stats.size());
  double max_deviation_percent = 0.0;
  if (mean > 0.0) {
    for (const NodeStats& entry : node_stats) {
      const double deviation = std::abs(static_cast<double>(entry.loaded) - mean) / mean * 100.0;
      max_deviation_percent = deviation > max_deviation_percent ? deviation : max_deviation_percent;
    }
  }
  out << std::fixed << std::setprecision(2);
  out << "DIST mean=" << mean << " min=" << minimum << " max=" << maximum
      << " max_deviation_percent=" << max_deviation_percent << '\n';

  for (const OwnershipVerification& scan : scans) {
    out << "VERIFY node=" << scan.node << " scanned=" << scan.scanned
        << " misowned=" << scan.misowned << " result=" << (scan.misowned == 0U ? "PASS" : "FAIL")
        << '\n';
  }
  out << "VERIFY ownership=" << (invariants.ownership ? "PASS" : "FAIL")
      << " uniqueness=" << (invariants.uniqueness ? "PASS" : "FAIL")
      << " accounting=" << (invariants.accounting ? "PASS" : "FAIL")
      << " completion=" << (invariants.completion ? "PASS" : "FAIL") << '\n';

  if (manifest.has_value()) {
    const bool matched = report_matches_manifest(config, *manifest, node_stats);
    out << "EXPECT manifest=" << (matched ? "PASS" : "FAIL") << " unique=" << total_unique
        << " duplicates=" << total_duplicates << '\n';
  }

  const double elapsed_seconds =
      elapsed.count() > 0 ? static_cast<double>(elapsed.count()) / 1000.0 : 0.001;
  const std::uint64_t records_per_second =
      static_cast<std::uint64_t>(static_cast<double>(total_parsed) / elapsed_seconds);
  const double input_mb_per_second =
      static_cast<double>(input_bytes_total) / 1'000'000.0 / elapsed_seconds;
  out << "TIMING elapsed_ms=" << elapsed.count() << " records_per_s=" << records_per_second
      << " input_mb_per_s=" << std::setprecision(1) << input_mb_per_second << '\n';

  return out.str();
}

void verify_report(const std::filesystem::path& report_path, const Manifest& manifest) {
  const SavedReport report = read_saved_report(report_path);
  if (report.version != manifest.version || report.files != manifest.files ||
      report.lines != manifest.lines || report.unique != manifest.unique ||
      report.duplicates != manifest.duplicates || report.loaded != manifest.expected_per_node) {
    report_error(ErrorCode::ManifestMismatch, "report does not match manifest", report_path);
  }
}

}
