// SPDX-License-Identifier: MIT
// THESIS: the model's real controls are the interface.
// OWN-WORLD: Ardor's dark neutral surface, bone text, native Cocoa controls.
// STORY: open a NAM, play and adjust physical controls, compare exported sizes.
// FIRST VIEWPORT: model header; scrollable controls; compact input/output footer.
// FORM: one resizable instrument panel, no chain browser or decorative widgets.
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "Editor.h"
#include <algorithm>
#include <cmath>

using namespace ardor::nam_clap;
namespace {
NSString* ns(const std::string& text) {return [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding]?:@"";}
NSTextField* label(NSString* text,CGFloat size=13,NSFontWeight weight=NSFontWeightRegular) {
  NSTextField* field=[NSTextField labelWithString:text];field.font=[NSFont systemFontOfSize:size weight:weight];
  field.lineBreakMode=NSLineBreakByTruncatingTail;field.translatesAutoresizingMaskIntoConstraints=NO;return field;
}
NSStackView* stack(NSArray<NSView*>* views,NSUserInterfaceLayoutOrientation axis,CGFloat spacing=12) {
  auto* view=[NSStackView stackViewWithViews:views];view.orientation=axis;
  view.alignment=axis==NSUserInterfaceLayoutOrientationVertical?NSLayoutAttributeLeading:NSLayoutAttributeCenterY;
  view.spacing=spacing;view.translatesAutoresizingMaskIntoConstraints=NO;return view;
}
NSView* spacer() {auto* view=[NSView new];[view setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];return view;}
void fullWidth(NSView* child,NSView* parent) {[child.widthAnchor constraintEqualToAnchor:parent.widthAnchor].active=YES;}
}

@interface NamDocumentStack : NSStackView
@end
@implementation NamDocumentStack
- (BOOL)isFlipped {return YES;}
@end

@interface NamSlider : NSSlider
@property(nonatomic,assign) EditorCallbacks* callbacks;
@property(nonatomic,assign) uint32_t parameter;
@property(nonatomic,assign) BOOL trackingGesture;
@end
@implementation NamSlider
- (void)mouseDown:(NSEvent*)event {
  self.trackingGesture=YES;if(self.callbacks)self.callbacks->gesture(self.parameter,true);
  @try {[super mouseDown:event];} @finally {if(self.callbacks)self.callbacks->gesture(self.parameter,false);self.trackingGesture=NO;}
}
- (void)keyDown:(NSEvent*)event {
  self.trackingGesture=YES;if(self.callbacks)self.callbacks->gesture(self.parameter,true);
  @try {[super keyDown:event];} @finally {if(self.callbacks)self.callbacks->gesture(self.parameter,false);self.trackingGesture=NO;}
}
@end

@interface NamEditorView : NSView <NSTextFieldDelegate> {
@public
  EditorCallbacks* callbacks_;
  std::vector<Control> definitions_;
  uint64_t revision_;
}
@property(nonatomic,strong) NSStackView* content;
@property(nonatomic,strong) NSStackView* modelControls;
@property(nonatomic,strong) NSScrollView* scroll;
@property(nonatomic,strong) NSTextField* modelName;
@property(nonatomic,strong) NSTextField* modelDetail;
@property(nonatomic,strong) NSTextField* status;
@property(nonatomic,strong) NSButton* openButton;
@property(nonatomic,strong) NSButton* resetButton;
@property(nonatomic,strong) NSPopUpButton* sizes;
@property(nonatomic,strong) NSButton* calibrate;
@property(nonatomic,strong) NSStackView* calibrationBody;
@property(nonatomic,strong) NSPopover* calibrationPopover;
@property(nonatomic,strong) NSTextField* reference;
@property(nonatomic,strong) NSTextField* calibrationNote;
@property(nonatomic,strong) NSMutableDictionary<NSNumber*,NSControl*>* controls;
@property(nonatomic,strong) NSMutableDictionary<NSNumber*,NSTextField*>* fields;
@property(nonatomic,strong) NSTimer* refreshTimer;
@property(nonatomic,strong) NSLevelIndicator* inputMeter;
@property(nonatomic,strong) NSLevelIndicator* outputMeter;
- (instancetype)initWithCallbacks:(EditorCallbacks*)callbacks;
- (void)refresh;
@end

@implementation NamEditorView
- (BOOL)isFlipped {return YES;}
- (instancetype)initWithCallbacks:(EditorCallbacks*)callbacks {
  self=[super initWithFrame:NSMakeRect(0,0,720,640)];if(!self)return nil;
  callbacks_=callbacks;revision_=0;
  self.controls=[NSMutableDictionary new];self.fields=[NSMutableDictionary new];
  self.wantsLayer=YES;self.layer.backgroundColor=[NSColor colorWithSRGBRed:.075 green:.083 blue:.09 alpha:1].CGColor;
  self.appearance=[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
  self.modelName=label(@"No model loaded",16,NSFontWeightMedium);
  self.modelDetail=label(@"Open a .nam file to begin.",12);self.modelDetail.textColor=NSColor.secondaryLabelColor;
  auto* heading=stack(@[label(@"Ardor NAM",22,NSFontWeightSemibold),spacer()],NSUserInterfaceLayoutOrientationHorizontal);
  self.openButton=[NSButton buttonWithTitle:@"Open Model…" target:self action:@selector(openModel:)];
  self.openButton.bezelStyle=NSBezelStyleRounded;self.openButton.keyEquivalent=@"o";
  self.openButton.keyEquivalentModifierMask=NSEventModifierFlagCommand;
  [heading addArrangedSubview:self.openButton];
  auto* identity=stack(@[self.modelName,self.modelDetail],NSUserInterfaceLayoutOrientationVertical,4);
  fullWidth(self.modelName,identity);fullWidth(self.modelDetail,identity);
  self.sizes=[[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];self.sizes.tag=Parameter::Size;
  [self.sizes addItemWithTitle:@"Single model"];self.sizes.enabled=NO;self.sizes.target=self;self.sizes.action=@selector(controlChanged:);
  [self.sizes setAccessibilityLabel:@"Model size"];
  auto* sizeRow=stack(@[label(@"Model size"),self.sizes,spacer()],NSUserInterfaceLayoutOrientationHorizontal);
  self.resetButton=[NSButton buttonWithTitle:@"Reset Controls" target:self action:@selector(resetControls:)];
  self.resetButton.bezelStyle=NSBezelStyleInline;
  auto* controlsHeading=stack(@[label(@"Model controls",13,NSFontWeightSemibold),spacer(),self.resetButton],NSUserInterfaceLayoutOrientationHorizontal);
  self.modelControls=[NamDocumentStack stackViewWithViews:@[]];
  self.modelControls.orientation=NSUserInterfaceLayoutOrientationVertical;self.modelControls.alignment=NSLayoutAttributeLeading;
  self.modelControls.spacing=12;self.modelControls.translatesAutoresizingMaskIntoConstraints=NO;
  self.scroll=[NSScrollView new];self.scroll.translatesAutoresizingMaskIntoConstraints=NO;
  self.scroll.drawsBackground=NO;self.scroll.borderType=NSNoBorder;self.scroll.hasVerticalScroller=YES;
  self.scroll.documentView=self.modelControls;
  [self.modelControls.leadingAnchor constraintEqualToAnchor:self.scroll.contentView.leadingAnchor constant:0].active=YES;
  [self.modelControls.trailingAnchor constraintEqualToAnchor:self.scroll.contentView.trailingAnchor constant:-12].active=YES;
  [self.modelControls.topAnchor constraintEqualToAnchor:self.scroll.contentView.topAnchor constant:4].active=YES;
  [self.scroll.heightAnchor constraintGreaterThanOrEqualToConstant:90].active=YES;
  auto* rule=[NSBox new];rule.boxType=NSBoxSeparator;
  auto* mode=[[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];mode.tag=Channel;
  [mode addItemsWithTitles:@[@"Left",@"Right",@"Average"]];mode.target=self;mode.action=@selector(controlChanged:);
  [mode setAccessibilityLabel:@"Guitar input"];self.controls[@(Channel)]=mode;
  auto* bypass=[NSButton checkboxWithTitle:@"Bypass" target:self action:@selector(controlChanged:)];bypass.tag=Bypass;self.controls[@(Bypass)]=bypass;
  auto* routing=stack(@[label(@"Guitar input"),mode,spacer(),bypass],NSUserInterfaceLayoutOrientationHorizontal);
  auto* input=[self row:globalControls[Input-1]];auto* output=[self row:globalControls[Output-1]];
  self.inputMeter=[self meter:@"Input level"];self.outputMeter=[self meter:@"Output level"];
  auto* meters=stack(@[label(@"Input",11),self.inputMeter,spacer(),label(@"Output",11),self.outputMeter],NSUserInterfaceLayoutOrientationHorizontal,8);
  auto* disclosure=[NSButton buttonWithTitle:@"Input Calibration…" target:self action:@selector(toggleCalibration:)];disclosure.bezelStyle=NSBezelStyleInline;
  self.calibrate=[NSButton checkboxWithTitle:@"Apply recorded input calibration" target:self action:@selector(controlChanged:)];
  self.calibrate.tag=Calibrate;self.controls[@(Calibrate)]=self.calibrate;
  auto* referenceRow=[self row:globalControls[Reference-1]];self.reference=self.fields[@(Reference)];
  self.calibrationNote=label(@"This model has no recorded input reference.",11);self.calibrationNote.textColor=NSColor.secondaryLabelColor;
  self.calibrationBody=stack(@[self.calibrate,referenceRow,self.calibrationNote],NSUserInterfaceLayoutOrientationVertical,8);
  fullWidth(referenceRow,self.calibrationBody);fullWidth(self.calibrationNote,self.calibrationBody);
  auto* calibrationView=[[NSView alloc] initWithFrame:NSMakeRect(0,0,520,140)];
  [calibrationView addSubview:self.calibrationBody];
  [NSLayoutConstraint activateConstraints:@[
    [self.calibrationBody.leadingAnchor constraintEqualToAnchor:calibrationView.leadingAnchor constant:16],
    [self.calibrationBody.trailingAnchor constraintEqualToAnchor:calibrationView.trailingAnchor constant:-16],
    [self.calibrationBody.topAnchor constraintEqualToAnchor:calibrationView.topAnchor constant:16]]];
  self.calibrationPopover=[NSPopover new];self.calibrationPopover.behavior=NSPopoverBehaviorTransient;
  self.calibrationPopover.animates=NO;self.calibrationPopover.contentSize=NSMakeSize(520,140);
  self.calibrationPopover.contentViewController=[NSViewController new];self.calibrationPopover.contentViewController.view=calibrationView;
  self.status=label(@"Load a NAM model to begin.",12);self.status.textColor=NSColor.secondaryLabelColor;
  self.status.lineBreakMode=NSLineBreakByWordWrapping;self.status.maximumNumberOfLines=2;
  [self.status.heightAnchor constraintGreaterThanOrEqualToConstant:30].active=YES;
  self.content=stack(@[heading,identity,sizeRow,controlsHeading,self.scroll,rule,routing,input,output,meters,disclosure,self.status],NSUserInterfaceLayoutOrientationVertical,8);
  self.content.detachesHiddenViews=YES;[self addSubview:self.content];
  [NSLayoutConstraint activateConstraints:@[
    [self.content.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:24],
    [self.content.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-24],
    [self.content.topAnchor constraintEqualToAnchor:self.topAnchor constant:22],
    [self.content.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-16]]];
  for(NSView* child in self.content.arrangedSubviews)fullWidth(child,self.content);
  [self.scroll setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationVertical];
  [self.scroll setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationVertical];
  [self refresh];return self;
}
- (NSLevelIndicator*)meter:(NSString*)name {
  auto* view=[[NSLevelIndicator alloc] initWithFrame:NSMakeRect(0,0,120,12)];
  view.levelIndicatorStyle=NSLevelIndicatorStyleContinuousCapacity;view.minValue=0;view.maxValue=1;
  view.warningValue=.9;view.criticalValue=.98;view.editable=NO;[view.widthAnchor constraintEqualToConstant:120].active=YES;
  [view setAccessibilityLabel:name];return view;
}
- (NSView*)row:(const Control&)control {
  auto* name=label(ns(control.name));name.toolTip=ns(control.name);
  [name.widthAnchor constraintEqualToConstant:144].active=YES;
  NSView* editor;
  if(control.stepped()) {
    auto* menu=[[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    for(const auto& text:control.choices)[menu addItemWithTitle:ns(text)];
    menu.tag=control.id;menu.target=self;menu.action=@selector(controlChanged:);
    [menu setAccessibilityLabel:ns(control.name)];self.controls[@(control.id)]=menu;editor=menu;
  }else {
    auto* slider=[[NamSlider alloc] initWithFrame:NSZeroRect];slider.minValue=control.min;slider.maxValue=control.max;
    slider.doubleValue=control.initial;slider.continuous=YES;slider.target=self;slider.action=@selector(controlChanged:);
    slider.tag=control.id;slider.parameter=control.id;slider.callbacks=callbacks_;[slider setAccessibilityLabel:ns(control.name)];
    auto* field=[[NSTextField alloc] initWithFrame:NSZeroRect];field.tag=control.id;field.delegate=self;
    field.alignment=NSTextAlignmentRight;field.font=[NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightRegular];
    auto* format=[NSNumberFormatter new];format.numberStyle=NSNumberFormatterDecimalStyle;
    format.minimum=@(control.min);format.maximum=@(control.max);format.maximumFractionDigits=3;format.usesGroupingSeparator=NO;
    if(control.id==Input||control.id==Output){format.positiveSuffix=@" dB";format.negativeSuffix=@" dB";}
    if(control.id==Reference){format.positiveSuffix=@" dBu";format.negativeSuffix=@" dBu";}
    field.formatter=format;field.doubleValue=control.initial;[field.widthAnchor constraintEqualToConstant:88].active=YES;
    [field setAccessibilityLabel:[ns(control.name) stringByAppendingString:@" value"]];
    [slider.widthAnchor constraintGreaterThanOrEqualToConstant:120].active=YES;
    editor=stack(@[slider,field],NSUserInterfaceLayoutOrientationHorizontal,12);
    self.controls[@(control.id)]=slider;self.fields[@(control.id)]=field;
  }
  [editor setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
  auto* row=stack(@[name,editor],NSUserInterfaceLayoutOrientationHorizontal,16);
  [row.heightAnchor constraintEqualToConstant:30].active=YES;return row;
}
- (void)controlChanged:(NSControl*)sender {
  if(!callbacks_)return;
  const uint32_t id=static_cast<uint32_t>(sender.tag);double value=sender.doubleValue;
  if([sender isKindOfClass:NSPopUpButton.class])value=((NSPopUpButton*)sender).indexOfSelectedItem;
  else if([sender isKindOfClass:NSButton.class])value=((NSButton*)sender).state==NSControlStateValueOn?1:0;
  const bool tracking=[sender isKindOfClass:NamSlider.class]&&((NamSlider*)sender).trackingGesture;
  try{if(!tracking)callbacks_->gesture(id,true);callbacks_->change(id,value);if(!tracking)callbacks_->gesture(id,false);[self refresh];}
  catch(const std::exception& error){self.status.stringValue=ns(error.what());}
}
- (void)controlTextDidEndEditing:(NSNotification*)notification {
  NSTextField* field=notification.object;const uint32_t id=static_cast<uint32_t>(field.tag);
  if(!callbacks_)return;
  try {callbacks_->gesture(id,true);callbacks_->change(id,field.doubleValue);callbacks_->gesture(id,false);[self refresh];}
  catch(const std::exception& error){self.status.stringValue=ns(error.what());}
}
- (void)openModel:(id)sender {
  if(!callbacks_)return;
  auto* panel=[NSOpenPanel openPanel];panel.canChooseDirectories=NO;panel.allowsMultipleSelection=NO;
  UTType* namType=[UTType typeWithFilenameExtension:@"nam"];panel.allowedContentTypes=namType?@[namType]:@[UTTypeData];panel.title=@"Open NAM Model";panel.prompt=@"Open Model";
  __weak NamEditorView* weakSelf=self;
  [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response){
    NamEditorView* view=weakSelf;
    if(response==NSModalResponseOK&&view&&view->callbacks_&&panel.URL.fileSystemRepresentation){
      try{view->callbacks_->open(std::filesystem::path(panel.URL.fileSystemRepresentation));[view refresh];}
      catch(const std::exception& error){view.status.stringValue=ns(error.what());}
    }
  }];
}
- (void)resetControls:(id)sender {
  if(!callbacks_)return;
  for(const auto& c:definitions_){callbacks_->gesture(c.id,true);callbacks_->change(c.id,c.initial);callbacks_->gesture(c.id,false);}
  [self refresh];
}
- (void)toggleCalibration:(NSButton*)sender {
  if(self.calibrationPopover.shown)[self.calibrationPopover close];
  else [self.calibrationPopover showRelativeToRect:sender.bounds ofView:sender preferredEdge:NSRectEdgeMaxY];
}
- (void)refresh {
  if(!callbacks_)return;
  try {
    callbacks_->tick();const auto state=callbacks_->state();
    if(state.revision!=revision_) {
      revision_=state.revision;
      for(const auto& c:definitions_){[self.controls removeObjectForKey:@(c.id)];[self.fields removeObjectForKey:@(c.id)];}
      definitions_.clear();for(NSView* view in self.modelControls.arrangedSubviews.copy){[self.modelControls removeArrangedSubview:view];[view removeFromSuperview];}
      [self.sizes removeAllItems];
      if(state.model) {
        self.modelName.stringValue=ns(state.model->name);
        self.modelDetail.stringValue=state.model->gear=="pedal"?@"Pedal model · 48 kHz":@"Amp model · 48 kHz";
        for(const auto& tier:state.model->tiers)[self.sizes addItemWithTitle:ns(tier.name)];
        definitions_=state.model->controls;
        for(const auto& c:definitions_){auto* row=[self row:c];[self.modelControls addArrangedSubview:row];fullWidth(row,self.modelControls);}
        if(definitions_.empty())[self.modelControls addArrangedSubview:label(@"This capture has no modeled controls.")];
      }else {
        self.modelName.stringValue=@"No model loaded";self.modelDetail.stringValue=@"Open a .nam file to begin.";
        [self.sizes addItemWithTitle:@"Single model"];
        [self.modelControls addArrangedSubview:label(@"Load a NAM model to see its controls.")];
      }
      self.sizes.enabled=state.model&&state.model->tiers.size()>1&&!state.loading;
      self.openButton.enabled=!state.loading;self.resetButton.enabled=!definitions_.empty()&&!state.loading;
      const bool calibrated=state.model&&state.model->inputLevel.has_value();self.calibrate.enabled=calibrated;
      if(calibrated)self.calibrationNote.stringValue=[NSString stringWithFormat:@"Model reference: %+.2f dBu. Set your interface's matching full-scale reference.",*state.model->inputLevel];
      else self.calibrationNote.stringValue=@"This model has no recorded input reference. Use Input trim to set the drive level.";
      self.status.stringValue=ns(state.status);
    }
    const auto selected=static_cast<NSInteger>(callbacks_->value(Parameter::Size));
    if(selected>=0&&selected<self.sizes.numberOfItems)[self.sizes selectItemAtIndex:selected];
    for(NSNumber* key in self.controls) {
      NSControl* control=self.controls[key];const double value=callbacks_->value(key.unsignedIntValue);
      if([control isKindOfClass:NamSlider.class]&&((NamSlider*)control).trackingGesture)continue;
      if([control isKindOfClass:NSPopUpButton.class])[(NSPopUpButton*)control selectItemAtIndex:static_cast<NSInteger>(value)];
      else if([control isKindOfClass:NSButton.class])((NSButton*)control).state=value>=.5?NSControlStateValueOn:NSControlStateValueOff;
      else control.doubleValue=value;
      NSTextField* field=self.fields[key];if(field&&!field.currentEditor)field.doubleValue=value;
    }
    const bool enabled=self.calibrate.enabled&&callbacks_->value(Calibrate)>=.5;
    self.reference.enabled=enabled;self.controls[@(Reference)].enabled=enabled;
    auto meterValue=[](float peak){return peak>0?std::clamp((20*std::log10(peak)+60)/60.f,0.f,1.f):0.f;};
    self.inputMeter.doubleValue=meterValue(state.inputPeak);self.outputMeter.doubleValue=meterValue(state.outputPeak);
  }catch(const std::exception& error){self.status.stringValue=ns(error.what());}
}
@end

namespace ardor::nam_clap {
namespace {
class MacEditor final:public Editor {
public:
  explicit MacEditor(EditorCallbacks callbacks):callbacks_(std::move(callbacks)) {
    if(!NSThread.isMainThread)throw std::runtime_error("The NAM editor must be created on the main thread");
    view_=[[NamEditorView alloc] initWithCallbacks:&callbacks_];view_.hidden=YES;
    if(!view_)throw std::runtime_error("Cannot create NAM editor");
  }
  ~MacEditor() override {show(false);[view_.calibrationPopover close];view_->callbacks_=nullptr;for(NSControl* c in view_.controls.allValues)if([c isKindOfClass:NamSlider.class])((NamSlider*)c).callbacks=nullptr;[view_ removeFromSuperview];}
  bool parent(void* parent) override {
    NSView* host=(__bridge NSView*)parent;
    if(!host||![host isKindOfClass:NSView.class]||!NSThread.isMainThread)return false;
    [host addSubview:view_];return true;
  }
  bool resize(uint32_t width,uint32_t height) override {
    if(width<600||width>1400||height<600||height>1200)return false;
    [view_ setFrameSize:NSMakeSize(width,height)];return true;
  }
  bool show(bool show) override {
    if(show&&!view_.superview)return false;
    view_.hidden=!show;
    if(show&&!view_.refreshTimer) {
      __weak NamEditorView* weakView=view_;
      view_.refreshTimer=[NSTimer timerWithTimeInterval:1./20 repeats:YES block:^(NSTimer*){[weakView refresh];}];
      [NSRunLoop.mainRunLoop addTimer:view_.refreshTimer forMode:NSRunLoopCommonModes];[view_ refresh];
    }else if(!show){[view_.refreshTimer invalidate];view_.refreshTimer=nil;}
    return true;
  }
  uint32_t width() const override{return static_cast<uint32_t>(view_.frame.size.width);}
  uint32_t height() const override{return static_cast<uint32_t>(view_.frame.size.height);}
private:
  EditorCallbacks callbacks_;NamEditorView* __strong view_;
};
}
std::unique_ptr<Editor> createEditor(EditorCallbacks callbacks){return std::make_unique<MacEditor>(std::move(callbacks));}
}
