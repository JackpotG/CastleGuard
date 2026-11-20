/****************************************************************************
 Copyright (c) 2010 cocos2d-x.org
 Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
 Copyright (c) 2019-present Axmol Engine contributors (see AUTHORS.md).

 https://axmol.dev/

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ****************************************************************************/

#include "AppDelegate.h"
#include "axmol.h"
#import <Foundation/Foundation.h>

#include <cstdio>

using namespace ax;

static bool configureWritablePath()
{
    NSFileManager* fileManager = [NSFileManager defaultManager];
    NSURL* supportDirectory = [[fileManager URLsForDirectory:NSApplicationSupportDirectory
                                                  inDomains:NSUserDomainMask] firstObject];
    NSString* bundleIdentifier = [[NSBundle mainBundle] bundleIdentifier];
    if (!supportDirectory || !bundleIdentifier.length)
    {
        std::fprintf(stderr, "[StartupStorage] Application Support or bundle identifier unavailable\n");
        return false;
    }

    NSURL* gameDirectory = [supportDirectory URLByAppendingPathComponent:bundleIdentifier isDirectory:YES];
    NSError* error = nil;
    if (![fileManager createDirectoryAtURL:gameDirectory
               withIntermediateDirectories:YES attributes:nil error:&error])
    {
        std::fprintf(stderr, "[StartupStorage] Could not create data directory: %s\n",
                     error ? [[error localizedDescription] UTF8String] : "unknown error");
        return false;
    }

    auto* fileUtils = FileUtils::getInstance();
    if (!fileUtils)
    {
        std::fprintf(stderr, "[StartupStorage] FileUtils initialization failed\n");
        return false;
    }
    fileUtils->setWritablePath([[gameDirectory path] UTF8String]);
    return true;
}

int axmol_main()
{
    // create the application instance
    AppDelegate app;
    return Application::getInstance()->run();
}

int main(int argc, char* argv[])
{
    @autoreleasepool
    {
        if (!configureWritablePath())
            return 1;
    }

#if _AX_TESTS
    AppDelegate app;
    return app.run(argc, argv);
#endif

    auto result = axmol_main();

#if AX_OBJECT_LEAK_DETECTION
    Object::printLeaks();
#endif

    return result;
}
