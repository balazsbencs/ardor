#include "ui/CloudClaimOverlay.h"

#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

namespace ardor {
namespace {

nlohmann::json readObject(const std::filesystem::path& path)
{
  std::ifstream input(path);
  if (!input) return nlohmann::json::object();
  try {
    auto value = nlohmann::json::parse(input);
    return value.is_object() ? std::move(value) : nlohmann::json::object();
  } catch (...) {
    return nlohmann::json::object();
  }
}

lv_obj_t* labeledButton(lv_obj_t* parent, const char* text, int32_t x)
{
  auto* button = lv_button_create(parent);
  lv_obj_set_size(button, 180, 58);
  lv_obj_align(button, LV_ALIGN_BOTTOM_MID, x, -28);
  auto* label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return button;
}

} // namespace

CloudClaimOverlay::CloudClaimOverlay(std::filesystem::path dataRoot)
  : directory_(std::move(dataRoot) / "runtime" / "cloud-claim"),
    codePath_(directory_ / "code.json"),
    pendingPath_(directory_ / "pending.json"),
    decisionPath_(directory_ / "decision.json")
{
}

CloudClaimOverlay::~CloudClaimOverlay()
{
  hide();
}

void CloudClaimOverlay::poll()
{
  const auto now = std::chrono::steady_clock::now();
  if (now < nextPoll_) return;
  nextPoll_ = now + std::chrono::milliseconds(250);

  const auto pending = readObject(pendingPath_);
  if (pending.value("version", 0) == 1) {
    const auto flowId = pending.value("claimFlowId", std::string{});
    const auto accountName = pending.value("accountDisplayName", std::string{});
    if (!flowId.empty() && !accountName.empty()) {
      const auto decision = readObject(decisionPath_);
      if (decision.value("version", 0) == 1
          && decision.value("claimFlowId", std::string{}) == flowId) {
        if (mode_ != Mode::Decided || flowId_ != flowId) {
          flowId_ = flowId;
          showDecided(decision.value("approved", false));
        }
      } else if (mode_ != Mode::Pending || flowId_ != flowId) {
        showPending(flowId, accountName);
      }
      return;
    }
  }

  const auto code = readObject(codePath_);
  if (code.value("version", 0) == 1) {
    const auto flowId = code.value("claimFlowId", std::string{});
    const auto manualCode = code.value("manualCode", std::string{});
    if (!flowId.empty() && !manualCode.empty()) {
      if (mode_ != Mode::Code || flowId_ != flowId) showCode(flowId, manualCode);
      return;
    }
  }

  hide();
}

bool CloudClaimOverlay::handleFootswitch(int index)
{
  if (!active()) return false;
  if (mode_ == Mode::Pending) {
    if (index == 0) decide(true);
    else if (index == 3) decide(false);
  }
  return true;
}

bool CloudClaimOverlay::recordPhysicalDecision(bool approved, std::string& error)
{
  const auto pending = readObject(pendingPath_);
  const auto flowId = pending.value("claimFlowId", std::string{});
  if (pending.value("version", 0) != 1 || flowId.empty()) {
    error = "no valid cloud claim is pending";
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(directory_, ec);
  if (ec) {
    error = "could not create cloud claim state directory";
    return false;
  }
  auto temporary = decisionPath_;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
      error = "could not create cloud claim decision";
      return false;
    }
    output << nlohmann::json{
      {"version", 1}, {"claimFlowId", flowId}, {"approved", approved},
      {"decidedAt", std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()},
    }.dump(2) << '\n';
    output.flush();
    if (!output) {
      error = "could not write cloud claim decision";
      return false;
    }
  }
  std::filesystem::permissions(
    temporary, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
    std::filesystem::perm_options::replace, ec);
  if (ec) {
    std::filesystem::remove(temporary, ec);
    error = "could not secure cloud claim decision";
    return false;
  }
  std::filesystem::rename(temporary, decisionPath_, ec);
  if (ec) {
    std::filesystem::remove(decisionPath_, ec);
    ec.clear();
    std::filesystem::rename(temporary, decisionPath_, ec);
  }
  if (ec) {
    std::filesystem::remove(temporary, ec);
    error = "could not publish cloud claim decision";
    return false;
  }
  return true;
}

void CloudClaimOverlay::showCode(const std::string& flowId, const std::string& code)
{
  hide();
  flowId_ = flowId;
  mode_ = Mode::Code;
  modal_ = lv_obj_create(lv_layer_top());
  lv_obj_set_size(modal_, 620, 300);
  lv_obj_center(modal_);
  title_ = lv_label_create(modal_);
  lv_label_set_text(title_, "Claim this Ardor pedal");
  lv_obj_align(title_, LV_ALIGN_TOP_MID, 0, 28);
  detail_ = lv_label_create(modal_);
  const auto text = std::string("Sign in to the hosted manager and enter\n\n") + code;
  lv_label_set_text(detail_, text.c_str());
  lv_obj_set_style_text_align(detail_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(detail_, LV_ALIGN_CENTER, 0, 12);
}

void CloudClaimOverlay::showPending(const std::string& flowId, const std::string& accountName)
{
  hide();
  flowId_ = flowId;
  mode_ = Mode::Pending;
  modal_ = lv_obj_create(lv_layer_top());
  lv_obj_set_size(modal_, 700, 360);
  lv_obj_center(modal_);
  title_ = lv_label_create(modal_);
  lv_label_set_text(title_, "Confirm device claim");
  lv_obj_align(title_, LV_ALIGN_TOP_MID, 0, 28);
  detail_ = lv_label_create(modal_);
  const auto text = accountName + " wants to claim this pedal.\nOnly approve if you started this request.\n\nFootswitch 1: Approve    Footswitch 4: Reject";
  lv_label_set_text(detail_, text.c_str());
  lv_obj_set_style_text_align(detail_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(detail_, LV_ALIGN_CENTER, 0, -20);
  approve_ = labeledButton(modal_, "Approve", -110);
  reject_ = labeledButton(modal_, "Reject", 110);
  lv_obj_add_event_cb(approve_, approveClicked, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(reject_, rejectClicked, LV_EVENT_CLICKED, this);
}

void CloudClaimOverlay::showDecided(bool approved)
{
  if (!modal_) {
    modal_ = lv_obj_create(lv_layer_top());
    lv_obj_set_size(modal_, 620, 260);
    lv_obj_center(modal_);
    title_ = lv_label_create(modal_);
    lv_obj_align(title_, LV_ALIGN_TOP_MID, 0, 28);
    detail_ = lv_label_create(modal_);
    lv_obj_align(detail_, LV_ALIGN_CENTER, 0, 16);
  }
  mode_ = Mode::Decided;
  lv_label_set_text(title_, approved ? "Claim approved" : "Claim rejected");
  lv_label_set_text(detail_, "Waiting for the hosted service to acknowledge the decision...");
  if (approve_) lv_obj_add_flag(approve_, LV_OBJ_FLAG_HIDDEN);
  if (reject_) lv_obj_add_flag(reject_, LV_OBJ_FLAG_HIDDEN);
}

void CloudClaimOverlay::hide()
{
  if (modal_) lv_obj_delete(modal_);
  modal_ = nullptr;
  title_ = nullptr;
  detail_ = nullptr;
  approve_ = nullptr;
  reject_ = nullptr;
  mode_ = Mode::Hidden;
  flowId_.clear();
}

void CloudClaimOverlay::decide(bool approved)
{
  std::string error;
  if (recordPhysicalDecision(approved, error)) showDecided(approved);
  else if (detail_) lv_label_set_text(detail_, error.c_str());
}

void CloudClaimOverlay::approveClicked(lv_event_t* event)
{
  static_cast<CloudClaimOverlay*>(lv_event_get_user_data(event))->decide(true);
}

void CloudClaimOverlay::rejectClicked(lv_event_t* event)
{
  static_cast<CloudClaimOverlay*>(lv_event_get_user_data(event))->decide(false);
}

} // namespace ardor
