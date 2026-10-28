// SPDX-License-Identifier: MIT
#import <AppKit/AppKit.h>
#include "TestHost.h"

void runLoop() {[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.12]];}
NSView* findView(NSView* root,NSString* name) {
  if([root.accessibilityLabel isEqualToString:name])return root;
  for(NSView* child in root.subviews)if(auto* found=findView(child,name))return found;
  return nil;
}
NSScrollView* findScroll(NSView* root) {
  if([root isKindOfClass:NSScrollView.class])return (NSScrollView*)root;
  for(NSView* child in root.subviews)if(auto* found=findScroll(child))return found;
  return nil;
}
void capture(NSView* view,const char* name) {
  const char* directory=std::getenv("ARDOR_NAM_SCREENSHOTS");if(!directory)return;
  std::filesystem::create_directories(directory);[view layoutSubtreeIfNeeded];[view displayIfNeeded];
  auto* image=[view bitmapImageRepForCachingDisplayInRect:view.bounds];
  [view cacheDisplayInRect:view.bounds toBitmapImageRep:image];
  NSData* png=[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
  const auto path=std::filesystem::path(directory)/(std::string(name)+".png");
  require([png writeToFile:[NSString stringWithUTF8String:path.c_str()] atomically:YES],"capture native editor");
}
struct OutputEvents {
  unsigned remaining=0;std::vector<uint16_t> types;
  clap_output_events_t api{this,[](const clap_output_events_t* out,const clap_event_header_t* e){
    auto& s=*static_cast<OutputEvents*>(out->ctx);if(!s.remaining)return false;
    --s.remaining;s.types.push_back(e->type);return true;}};
};
int main(){@autoreleasepool{try{
  [NSApplication sharedApplication];
  auto* library=clap_test::open(NAM_CLAP_BINARY);require(library,clap_test::error());
  auto* entry=static_cast<const clap_plugin_entry_t*>(clap_test::symbol(library,"clap_entry"));
  require(entry&&entry->init(NAM_CLAP_BINARY),"entry");
  auto* factory=static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  {
    Instance instance(factory),other(factory);
    const auto* gui=static_cast<const clap_plugin_gui_t*>(instance.plugin->get_extension(instance.plugin,CLAP_EXT_GUI));
    require(gui&&gui->is_api_supported(instance.plugin,CLAP_WINDOW_API_COCOA,false),"embedded Cocoa API");
    require(!gui->create(instance.plugin,CLAP_WINDOW_API_X11,false)&&gui->create(instance.plugin,CLAP_WINDOW_API_COCOA,false),"GUI API negotiation");
    require(!gui->create(instance.plugin,CLAP_WINDOW_API_COCOA,false)&&!gui->show(instance.plugin),"duplicate and unparented GUI rejected");
    uint32_t width=0,height=0;require(gui->get_size(instance.plugin,&width,&height)&&width==720&&height==640,"logical initial size");
    NSWindow* window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,width,height)
      styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];window.releasedWhenClosed=NO;
    clap_window_t parent{};parent.api=CLAP_WINDOW_API_COCOA;parent.cocoa=(__bridge void*)window.contentView;
    require(gui->set_parent(instance.plugin,&parent)&&gui->show(instance.plugin),"native parent and show");
    [window orderFront:nil];runLoop();NSView* view=window.contentView.subviews.firstObject;
    require(view&&view.isFlipped,"native view exists");capture(view,"01-empty");
    require(instance.loadFile(NAM_CLAP_DEMO),"load demo");runLoop();capture(view,"02-demo");
    const auto drive=instance.find("Drive");auto* slider=(NSSlider*)findView(view,@"Drive");
    require(slider&&[slider isKindOfClass:NSSlider.class],"learned control in native UI");
    // Accessible native actions traverse the same target/action path as mouse/keyboard input.
    slider.doubleValue=8;[slider sendAction:slider.action to:slider.target];runLoop();
    require(instance.value(drive)==8&&other.find("Drive")==CLAP_INVALID_ID,"editor changes only its own instance");
    OutputEvents output;output.remaining=1;instance.params->flush(instance.plugin,nullptr,&output.api);
    require(output.types==std::vector<uint16_t>{CLAP_EVENT_PARAM_GESTURE_BEGIN},"gesture begins before value");
    output.remaining=10;instance.params->flush(instance.plugin,nullptr,&output.api);
    require(output.types==std::vector<uint16_t>{CLAP_EVENT_PARAM_GESTURE_BEGIN,CLAP_EVENT_PARAM_VALUE,CLAP_EVENT_PARAM_GESTURE_END},"host backpressure retains value and gesture end");
    for(unsigned i=0;i<400;++i){slider.doubleValue=i%11;[slider sendAction:slider.action to:slider.target];}
    output.types.clear();output.remaining=4096;instance.params->flush(instance.plugin,nullptr,&output.api);
    int gestures=0;
    for(const auto type:output.types){if(type==CLAP_EVENT_PARAM_GESTURE_BEGIN)++gestures;if(type==CLAP_EVENT_PARAM_GESTURE_END)--gestures;require(gestures>=0&&gestures<=1,"gestures remain ordered when the UI queue fills");}
    require(gestures==0,"a full UI queue retains every accepted gesture end");
    auto state=instance.save();auto document=Json::parse(state.data);
    document["model"]=readJson(std::filesystem::path(NAM_CLAP_ROOT)/"tests/fixtures/parametric-trainer-a2-dual.json")["models"][0]["model"];
    document["name"]="Two-tier trainer fixture.nam";document["controls"]={{"Drive",7},{"Mode",1}};document["tier"]=1;
    state.data=document.dump();require(instance.restore(state),"combined model");runLoop();capture(view,"03-combined");
    auto* sizes=(NSPopUpButton*)findView(view,@"Model size");auto* mode=(NSPopUpButton*)findView(view,@"Mode");
    require(sizes&&sizes.enabled&&sizes.numberOfItems==2&&[sizes.itemTitles[0] isEqualToString:@"Nano"]&&[sizes.itemTitles[1] isEqualToString:@"Standard"],"real model sizes");
    require(mode&&[mode.itemTitles[1] isEqualToString:@"Hard"],"exported mode labels");
    [sizes selectItemAtIndex:0];[sizes sendAction:sizes.action to:sizes.target];
    for(unsigned i=0;i<100&&instance.value(Parameter::Size)!=0;++i){instance.callbacks();runLoop();}
    for(unsigned i=0;i<10;++i){instance.callbacks();runLoop();}
    require(instance.value(Parameter::Size)==0&&instance.value(instance.find("Drive"))==7,"UI tier switch preserves controls");
    const auto fixture=readJson(std::filesystem::path(NAM_CLAP_ROOT)/"tests/fixtures/parametric-trainer-a2.json");
    auto found=std::find_if(fixture["models"].begin(),fixture["models"].end(),[](const auto& m){return ModelInfo::parse(m.at("model"),"").controls.size()==24;});
    require(found!=fixture["models"].end(),"24-control trainer fixture exists");
    document["model"]=found->at("model");document["controls"]=found->at("cases")[0].at("parameters");document["name"]="24-control trainer fixture.nam";document["tier"]=0;
    state.data=document.dump();require(instance.restore(state),"24 controls restore");runLoop();
    auto* scroll=findScroll(view);require(scroll&&scroll.documentView.frame.size.height>scroll.contentSize.height,"long parameter lists scroll");
    capture(view,"04-many-controls");
    width=1;height=1;require(gui->adjust_size(instance.plugin,&width,&height)&&width==600&&height==600,"minimum size hints");
    require(!gui->set_size(instance.plugin,1,1)&&gui->set_size(instance.plugin,width,height),"minimum size contract");
    runLoop();capture(view,"05-small");
    auto* input=findView(view,@"Input trim");auto* outputTrim=findView(view,@"Output trim");
    require(input&&outputTrim&&NSContainsRect(view.bounds,[input convertRect:input.bounds toView:view])&&NSContainsRect(view.bounds,[outputTrim convertRect:outputTrim.bounds toView:view]),"I/O controls remain inside minimum-size editor");
    require(gui->hide(instance.plugin)&&gui->show(instance.plugin),"hide and reopen");gui->destroy(instance.plugin);runLoop();
    require(window.contentView.subviews.count==0,"native view removed on destroy");
    require(gui->create(instance.plugin,CLAP_WINDOW_API_COCOA,false)&&gui->set_parent(instance.plugin,&parent)&&gui->show(instance.plugin),"recreate editor");
    runLoop();gui->destroy(instance.plugin);[window close];
  }
  entry->deinit(); // Cocoa class implementations retain the module until process exit.
  std::cout<<"Native NAM editor controls, gestures, resize, scrolling and lifecycle passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}}
