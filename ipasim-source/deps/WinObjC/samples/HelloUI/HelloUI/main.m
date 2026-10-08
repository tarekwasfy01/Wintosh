//
#import <UIKit/UIKit.h>

// [port] CHANGED: We export this entrypoint to call it manually from our
// [port] sample `HelloUIWrapper`.
#if defined(OBJC_PORT)
__declspec(dllexport)
#endif
int main(int argc, char* argv[])
{
    UIApplicationMain(0, NULL, nil, @"HelloUIApp");
    
    return 0;
}
