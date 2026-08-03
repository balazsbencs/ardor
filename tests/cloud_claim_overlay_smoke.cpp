#include "ui/CloudClaimOverlay.h"

#include <filesystem>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

int main()
{
  const auto root = std::filesystem::temp_directory_path() / "ardor-cloud-claim-overlay-smoke";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "runtime" / "cloud-claim");
  {
    std::ofstream pending(root / "runtime" / "cloud-claim" / "pending.json");
    pending << nlohmann::json{
      {"version", 1}, {"claimFlowId", "flow-id"}, {"correlationId", "message-id"},
      {"accountId", "account-id"}, {"accountDisplayName", "Alice"}, {"nonce", "nonce"},
      {"nextClaimEpoch", 1}, {"expiresAt", "2030-01-01T00:00:00Z"},
    }.dump(2);
  }
  ardor::CloudClaimOverlay overlay(root);
  std::string error;
  if (!overlay.recordPhysicalDecision(true, error)) {
    std::cerr << error << "\n";
    return 1;
  }
  std::ifstream decisionFile(root / "runtime" / "cloud-claim" / "decision.json");
  const auto decision = nlohmann::json::parse(decisionFile);
  if (decision.value("claimFlowId", "") != "flow-id" || !decision.value("approved", false)) {
    std::cerr << "physical decision was not persisted\n";
    return 1;
  }
  std::filesystem::remove_all(root, ec);
  return 0;
}
