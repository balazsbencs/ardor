#include "ui/LvglUiNavigation.h"

#include "ui/LvglUi.h"

namespace ardor::lvgl_navigation {
namespace {

void redraw(UiEventContext* context)
{
  context->ui->invalidate(UiChange::None);
}

} // namespace

void onSaveClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->ui->actions().savePreset) {
    context->ui->actions().savePreset();
  }
  redraw(context);
}

void onNavigationDecision(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (!context->ui->actions().resolveNavigation) return;
  const auto decision = context->index == 0 ? UiNavigationDecision::Save
    : context->index == 1 ? UiNavigationDecision::Discard : UiNavigationDecision::Cancel;
  context->ui->actions().resolveNavigation(decision);
  redraw(context);
}

void onPresetModeClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->state->mode == UiMode::Tuner && context->ui->actions().setTunerMode) {
    context->ui->actions().setTunerMode(false);
  } else {
    enterPresetMode(*context->state);
  }
  redraw(context);
}

void onTunerModeClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->ui->actions().setTunerMode) {
    context->ui->actions().setTunerMode(true);
  } else {
    enterTunerMode(*context->state);
  }
  redraw(context);
}

void onEditModeClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  enterEditMode(*context->state);
  redraw(context);
}

void onSettingsClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->openSettings(*context->state);
}

} // namespace ardor::lvgl_navigation
