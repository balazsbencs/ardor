#import <AppKit/AppKit.h>
#include "ClapEditor.h"
#include <algorithm>
#include <cmath>

using ardor::clap_editor::Canvas;

@interface ArdorClapEditorView : NSView
@property(nonatomic, assign) Canvas* canvas;
@property(nonatomic, strong) NSTimer* refreshTimer;
@end

@implementation ArdorClapEditorView
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)drawRect:(NSRect)rect {
  if (!self.canvas || self.canvas->pixels().empty()) return;
  auto* canvas = self.canvas;
  auto* provider = CGDataProviderCreateWithData(nullptr, canvas->pixels().data(), canvas->pixels().size(), nullptr);
  auto* color = CGColorSpaceCreateDeviceRGB();
  auto* image = CGImageCreate(canvas->pixelWidth(), canvas->pixelHeight(), 8, 32,
    canvas->pixelWidth() * 4, color, kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst,
    provider, nullptr, false, kCGRenderingIntentDefault);
  if (image) {
    auto* context = NSGraphicsContext.currentContext.CGContext;
    CGContextSaveGState(context);
    // LVGL stores rows from top to bottom; CoreGraphics images use a bottom origin.
    CGContextTranslateCTM(context, 0, self.bounds.size.height);
    CGContextScaleCTM(context, 1, -1);
    CGContextSetInterpolationQuality(context, kCGInterpolationNone);
    CGContextDrawImage(context, NSRectToCGRect(self.bounds), image);
    CGContextRestoreGState(context);
    CGImageRelease(image);
  }
  CGColorSpaceRelease(color);
  CGDataProviderRelease(provider);
}
- (void)resizeCanvas {
  if (!self.canvas || self.bounds.size.width < 1 || self.bounds.size.height < 45) return;
  const double scale = std::clamp(self.window ? self.window.backingScaleFactor : 1., 1., 3.);
  try {
    self.canvas->resize(static_cast<int>(std::round(self.bounds.size.width)),
      static_cast<int>(std::round(self.bounds.size.height)), scale);
    self.needsDisplay = YES;
  } catch (const std::exception& error) { NSLog(@"Ardor editor resize: %s", error.what()); }
}
- (void)viewDidMoveToWindow { [super viewDidMoveToWindow]; [self resizeCanvas]; }
- (void)viewDidChangeBackingProperties { [super viewDidChangeBackingProperties]; [self resizeCanvas]; }
- (void)setFrameSize:(NSSize)size { [super setFrameSize:size]; [self resizeCanvas]; }
- (void)pointerEvent:(NSEvent*)event pressed:(BOOL)pressed {
  if (!self.canvas) return;
  const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
  try { self.canvas->pointer(point.x, point.y, pressed); self.canvas->tick(); self.needsDisplay = YES; }
  catch (const std::exception& error) { NSLog(@"Ardor editor input: %s", error.what()); }
}
- (void)mouseDown:(NSEvent*)event { [self.window makeFirstResponder:self]; [self pointerEvent:event pressed:YES]; }
- (void)mouseDragged:(NSEvent*)event { [self pointerEvent:event pressed:YES]; }
- (void)mouseUp:(NSEvent*)event { [self pointerEvent:event pressed:NO]; }
- (void)keyEvent:(NSEvent*)event pressed:(BOOL)pressed {
  if (!self.canvas || !event.charactersIgnoringModifiers.length) return;
  const unichar character = [event.charactersIgnoringModifiers characterAtIndex:0];
  uint32_t key = character;
  switch (character) {
    case NSLeftArrowFunctionKey: key = LV_KEY_LEFT; break;
    case NSRightArrowFunctionKey: key = LV_KEY_RIGHT; break;
    case NSUpArrowFunctionKey: key = LV_KEY_UP; break;
    case NSDownArrowFunctionKey: key = LV_KEY_DOWN; break;
    case '\r': key = LV_KEY_ENTER; break;
    case '\t': key = (event.modifierFlags & NSEventModifierFlagShift) ? LV_KEY_PREV : LV_KEY_NEXT; break;
    case 0x1b: key = LV_KEY_ESC; break;
    case 0x7f: key = LV_KEY_BACKSPACE; break;
  }
  try { self.canvas->key(key, pressed); self.canvas->tick(); self.needsDisplay = YES; }
  catch (const std::exception& error) { NSLog(@"Ardor editor keyboard: %s", error.what()); }
}
- (void)keyDown:(NSEvent*)event { [self keyEvent:event pressed:YES]; }
- (void)keyUp:(NSEvent*)event { [self keyEvent:event pressed:NO]; }
@end

namespace ardor::clap_editor {
namespace {
class MacEditor final : public NativeEditor {
public:
  explicit MacEditor(Canvas& canvas) {
    view_ = [[ArdorClapEditorView alloc] initWithFrame:NSMakeRect(0, 0, 1100, 663)];
    view_.canvas = &canvas;
    view_.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    view_.hidden = YES;
    if (!view_) throw std::runtime_error("Cannot create Ardor Cocoa view");
  }
  ~MacEditor() override {
    hide();
    view_.canvas = nullptr;
    [view_ removeFromSuperview];
  }
  bool setParent(void* parent) override {
    if (!parent || ![NSThread isMainThread]) return false;
    NSView* host = (__bridge NSView*)parent;
    if (![host isKindOfClass:NSView.class]) return false;
    [host addSubview:view_];
    [view_ resizeCanvas];
    return true;
  }
  bool setSize(uint32_t width, uint32_t height) override {
    if (width < 960 || width > 1920 || height < 584 || height > 1124) return false;
    [view_ setFrameSize:NSMakeSize(width, height)];
    return true;
  }
  bool show() override {
    if (!view_.superview) return false;
    view_.hidden = NO;
    if (!view_.refreshTimer) {
      __weak ArdorClapEditorView* weakView = view_;
      view_.refreshTimer = [NSTimer timerWithTimeInterval:1. / 30 repeats:YES block:^(NSTimer*) {
        ArdorClapEditorView* view = weakView;
        if (!view.canvas) return;
        try { view.canvas->tick(); view.needsDisplay = YES; }
        catch (const std::exception& error) { NSLog(@"Ardor editor refresh: %s", error.what()); }
      }];
      [NSRunLoop.mainRunLoop addTimer:view_.refreshTimer forMode:NSRunLoopCommonModes];
    }
    [view_ resizeCanvas];
    return true;
  }
  bool hide() override {
    [view_.refreshTimer invalidate];
    view_.refreshTimer = nil;
    view_.hidden = YES;
    return true;
  }
  uint32_t width() const override { return static_cast<uint32_t>(std::round(view_.frame.size.width)); }
  uint32_t height() const override { return static_cast<uint32_t>(std::round(view_.frame.size.height)); }
private:
  ArdorClapEditorView* __strong view_ = nil;
};
}
std::unique_ptr<NativeEditor> createMacEditor(Canvas& canvas) { return std::make_unique<MacEditor>(canvas); }
} // namespace ardor::clap_editor
