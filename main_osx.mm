#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#define SOKOL_METAL
#include "sokol_gfx.h"

#include "yommd.hpp"

@interface AppDelegate: NSObject<NSApplicationDelegate>
@end

@interface Window: NSWindow
@end

@interface WindowDelegate: NSObject<NSWindowDelegate>
@end

@interface View: MTKView
@end

@interface ViewDelegate: NSObject<MTKViewDelegate>
@end

@interface AppMain: NSObject
-(void)createMainWindow;
-(void)createStatusItem;
-(void)actionQuit:(NSMenuItem *)sender;
-(void)actionToggleHandleMouse:(NSMenuItem *)sender;
-(const NSMenu *)getAppMenu;
-(sg_context_desc)getSokolContext;
-(id<CAMetalDrawable>)getDrawable;
-(MTLRenderPassDescriptor *)getRenderPassDescriptor;
-(NSSize)getWindowSize;
-(NSSize)getDrawableSize;
-(Routine&)getRoutine;
@end

static AppMain *appMain;
static const void *getSokolDrawable(void);
static const void *getSokolRenderpassDescriptor(void);

@implementation AppDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    appMain = [[AppMain alloc] init];
    [appMain createMainWindow];
    [appMain createStatusItem];

    auto& routine = [appMain getRoutine];
    routine.LoadMMD();
    routine.Init();
}
- (void)applicationWillTerminate:(NSNotification *)aNotification {
    [appMain getRoutine].Terminate();
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    return YES;
}
@end

@implementation Window
- (void)mouseDragged:(NSEvent *)event {
    [appMain getRoutine].OnMouseDragged();
}
- (void)mouseDown:(NSEvent *)event {
    [appMain getRoutine].OnMouseDown();
}
- (void)scrollWheel:(NSEvent *)event {
    if (event.hasPreciseScrollingDeltas) {
        [appMain getRoutine].OnWheelScrolled(event.scrollingDeltaY);
    }
}
- (BOOL)canBecomeKeyWindow {
    return NO;
}
@end

@implementation WindowDelegate
@end

@implementation View
-(BOOL)acceptsFirstMouse:(NSEvent *)event {
    return NO;
}
- (const NSMenu *)menuForEvent:(NSEvent *)event {
    return [appMain getAppMenu];
}
@end

@implementation ViewDelegate
- (void)mtkView:(nonnull MTKView*)view drawableSizeWillChange:(CGSize)size {
    Info::Log("drawableSize:", size.width, ',', size.height);
}
- (void)drawInMTKView:(nonnull MTKView*)view {
    @autoreleasepool {
        auto& routine = [appMain getRoutine];
        routine.Update();
        routine.Draw();
    }
}
@end

@implementation AppMain {
    AppDelegate *appDelegate;
    Window *window;
    WindowDelegate *windowDelegate;
    View *view;
    id<MTLDevice> metalDevice;
    ViewDelegate *viewDelegate;
    NSStatusItem *statusItem;
    NSMenu *appMenu;
    NSRunningApplication *alterApp;
    Routine routine;
}
-(void)createMainWindow {
    const NSUInteger style = NSWindowStyleMaskBorderless;
    // const NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;
    const auto screenRect = [NSScreen mainScreen].visibleFrame;
    const NSUInteger collectionBehavior =
        NSWindowCollectionBehaviorCanJoinAllSpaces |
        NSWindowCollectionBehaviorStationary |
        NSWindowCollectionBehaviorTransient |
        NSWindowCollectionBehaviorFullScreenAuxiliary |
        NSWindowCollectionBehaviorFullScreenDisallowsTiling |
        NSWindowCollectionBehaviorIgnoresCycle;

    window = [[Window alloc] initWithContentRect:screenRect
                                         styleMask:style
                                           backing:NSBackingStoreBuffered
                                             defer:NO];
    windowDelegate = [[WindowDelegate alloc] init];

    [window setTitle:@"yoMMD"];
    [window center];
    [window setIsVisible:YES];
    [window setOpaque:NO];
    [window setHasShadow:NO];
    [window setBackgroundColor:[NSColor clearColor]];
    [window setLevel:NSFloatingWindowLevel];
    [window setCollectionBehavior:collectionBehavior];
    [window setDelegate:windowDelegate];
    [window setIgnoresMouseEvents:YES];

    viewDelegate = [[ViewDelegate alloc] init];
    metalDevice = MTLCreateSystemDefaultDevice();
    view = [[View alloc] init];
    [view setPreferredFramesPerSecond:static_cast<NSInteger>(Constant::FPS)];
    [view setDelegate:viewDelegate];
    [view setDevice: metalDevice];
    [view setColorPixelFormat:MTLPixelFormatBGRA8Unorm];
    [view setDepthStencilPixelFormat:MTLPixelFormatDepth32Float_Stencil8];
    // [view setAutoResizeDrawable:NO];

    [window setContentView:view];
    [[view layer] setMagnificationFilter:kCAFilterNearest];
    [[view layer] setOpaque:NO];  // Make transparent
}
-(void)createStatusItem {
    NSImage *icon = [NSImage imageWithSystemSymbolName:@"face.smiling" accessibilityDescription:@"Smiling"];
    statusItem =
        [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];
    [statusItem.button setImage:icon];
    [statusItem setBehavior:NSStatusItemBehaviorTerminationOnRemoval];
    [statusItem setVisible:YES];

    appMenu = [[NSMenu alloc] init];

    NSMenuItem *enableMouse = [[NSMenuItem alloc]
                        initWithTitle:@"Enable Mouse"
                               action:@selector(actionToggleHandleMouse:)
                        keyEquivalent:@""];
    [enableMouse setTarget:self];
    [enableMouse setState:NSControlStateValueOff];

    NSMenuItem *quit = [[NSMenuItem alloc]
            initWithTitle:@"Quit" action:@selector(actionQuit:) keyEquivalent:@""];
    [quit setTarget:self];

    [appMenu addItem:enableMouse];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItem:quit];
    [statusItem setMenu:appMenu];

    alterApp = NULL;
}
-(void)actionQuit:(id)sender {
    [NSApp terminate:sender];
}
-(void)actionToggleHandleMouse:(NSMenuItem *)sender {
    if (sender.state == NSControlStateValueOff) {
        auto appList = [[NSWorkspace sharedWorkspace] runningApplications];
        for (NSUInteger i = 0; i < appList.count; ++i) {
            auto app = [appList objectAtIndex:i];
            if (app.active) {
                alterApp = app;
                break;
            }
        }

        [sender setState:NSControlStateValueOn];
        [window setIgnoresMouseEvents:NO];
    } else {
        [sender setState:NSControlStateValueOff];
        [window setIgnoresMouseEvents:YES];
        if (NSApp.active && alterApp) {
            [alterApp activateWithOptions:NSApplicationActivateIgnoringOtherApps];
        }
        alterApp = NULL;
    }
}
-(const NSMenu *)getAppMenu {
    return appMenu;
}
-(sg_context_desc)getSokolContext {
    return sg_context_desc{
        .sample_count = Constant::SampleCount,
        .metal = {
            .device = (__bridge const void *)metalDevice,
            .renderpass_descriptor_cb = getSokolRenderpassDescriptor,
            .drawable_cb = getSokolDrawable,
        },
    };
}
-(id<CAMetalDrawable>)getDrawable {
    return [view currentDrawable];
}
-(MTLRenderPassDescriptor *)getRenderPassDescriptor {
    return [view currentRenderPassDescriptor];
}
-(NSSize)getWindowSize {
    return window.frame.size;
}
-(NSSize)getDrawableSize {
    return view.drawableSize;
}
-(Routine&)getRoutine {
    return routine;
}
@end

static const void *getSokolDrawable() {
    return (__bridge const void *)[appMain getDrawable];
}

static const void *getSokolRenderpassDescriptor(void) {
    return (__bridge const void *)[appMain getRenderPassDescriptor];
}

sg_context_desc Context::getSokolContext() {
    return [appMain getSokolContext];
}

glm::vec2 Context::getWindowSize() {
    const auto size = [appMain getWindowSize];
    return glm::vec2(size.width, size.height);
}

glm::vec2 Context::getDrawableSize() {
    const auto size = [appMain getDrawableSize];
    return glm::vec2(size.width, size.height);
}

glm::vec2 Context::getMousePosition() {
    const auto pos = [NSEvent mouseLocation];
    return glm::vec2(pos.x, pos.y);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    [NSApplication sharedApplication];

    auto appDelegate = [[AppDelegate alloc] init];

    [NSApp setDelegate:appDelegate];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [NSApp activateIgnoringOtherApps:NO];
    [NSApp run];
}
