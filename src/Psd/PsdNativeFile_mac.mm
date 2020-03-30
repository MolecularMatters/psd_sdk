//
//  PsdNativeFile_mac.cpp
//  Contributed to psd_sdk
//
//  Created by Oluseyi Sonaiya on 3/29/20.
//  Copyright © 2020 Oluseyi Sonaiya. All rights reserved.
//
// psd_sdk Copyright 2011-2020, Molecular Matters GmbH <office@molecular-matters.com>
// See LICENSE.txt for licensing details (2-clause BSD License: https://opensource.org/licenses/BSD-2-Clause)

#include <wchar.h>

#include "PsdPch.h"
#include "PsdNativeFile_mac.h"

#include "PsdAllocator.h"
#include "PsdPlatform.h"
#include "PsdMemoryUtil.h"
#include "PsdLog.h"
#include "Psdinttypes.h"


PSD_NAMESPACE_BEGIN

typedef void (^ReadabilityHandler)(NSFileHandle *);
typedef void (^WriteabilityHandler)(NSFileHandle *);


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
NativeFile::NativeFile(Allocator* allocator)
    : File(allocator)
    , fileHandle(nil)
{
}


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
bool NativeFile::DoOpenRead(const wchar_t* filename)
{
    NSString *path = [[NSString alloc] initWithBytes:filename
                                              length:(wcslen(filename) * sizeof(*filename))
                                            encoding:NSUTF32LittleEndianStringEncoding];
    fileHandle = [NSFileHandle fileHandleForReadingAtPath:path];
    if (fileHandle == nil) {
        PSD_ERROR("NativeFile", "Cannot obtain handle for file \"%ls\".", filename);
        return false;
    }

    return true;
}


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
bool NativeFile::DoOpenWrite(const wchar_t* filename)
{
    NSString *path = [[NSString alloc] initWithBytes:filename
                                              length:(wcslen(filename) * sizeof(*filename))
                                            encoding:NSUTF32LittleEndianStringEncoding];
    fileHandle = [NSFileHandle fileHandleForWritingAtPath:path];
    if (fileHandle == nil) {
        PSD_ERROR("NativeFile", "Cannot obtain handle for file \"%ls\".", filename);
        return false;
    }

    return true;
}


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
bool NativeFile::DoClose(void)
{
    if (!fileHandle)
        return false;
    
    [fileHandle synchronizeFile];
    const BOOL success = [fileHandle closeAndReturnError:nil];
    if  (success == 0)
    {
        PSD_ERROR("NativeFile", "Cannot close handle.");
        return false;
    }

    fileHandle = nil;
    return true;
}


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
File::ReadOperation NativeFile::DoRead(void* buffer, uint32_t count, uint64_t position)
{
    if (!fileHandle) {
        PSD_ERROR("NativeFile", "Attempt to read from closed file");
        return nullptr;
    }
    
    [fileHandle synchronizeFile];
    [fileHandle seekToFileOffset:position];
    fileIOcompleted = dispatch_semaphore_create(0);
    fileHandle.readabilityHandler = ^void (NSFileHandle *handle) {
        NSError *error;
        [handle readDataUpToLength:count error:&error];
        if (error != nil) {
            PSD_ERROR("NativeFile", "Cannot read %u bytes from file position %" PRIu64 " asynchronously.", count, position);
        }
        dispatch_semaphore_signal(fileIOcompleted);
    };
    
    void *op = (__bridge void *)fileHandle;
    return static_cast<File::ReadOperation>(op);
}


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
bool NativeFile::DoWaitForRead(File::ReadOperation& operation)
{
//    void *op = static_cast<void *>(operation);
    
    // fix timeout to some reasonable interval
    long finished = dispatch_semaphore_wait(fileIOcompleted, DISPATCH_TIME_FOREVER);
    fileHandle.readabilityHandler = nil;
    fileIOcompleted = nil;
    
    if (finished != 0)
    {
        PSD_ERROR("NativeFile", "Failed to wait for previous asynchronous read operation.");
        return false;
    }

    return true;
}


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
File::WriteOperation NativeFile::DoWrite(const void* buffer, uint32_t count, uint64_t position)
{
    if (!fileHandle) {
        PSD_ERROR("NativeFile", "Attempt to write to closed file");
        return nullptr;
    }
    
    [fileHandle synchronizeFile];
    [fileHandle seekToFileOffset:position];
    fileIOcompleted = dispatch_semaphore_create(0);
    fileHandle.writeabilityHandler = ^void (NSFileHandle *handle) {
        NSData *data = [NSData dataWithBytes:buffer length:count];
        NSError *error;
        [handle writeData:data error:&error];
        if (error != nil) {
            PSD_ERROR("NativeFile", "Cannot write %u bytes from file position %" PRIu64 " asynchronously.", count, position);
        }
        dispatch_semaphore_signal(fileIOcompleted);
    };

    void *op = (__bridge void *)fileHandle;
    return static_cast<File::ReadOperation>(op);
}


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
bool NativeFile::DoWaitForWrite(File::WriteOperation& operation)
{
//    void *op = static_cast<void *>(operation);
    
    // fix timeout to some reasonable interval
    long finished = dispatch_semaphore_wait(fileIOcompleted, DISPATCH_TIME_FOREVER);
    fileHandle.writeabilityHandler = nil;
    fileIOcompleted = nil;
    
    if (finished != 0)
    {
        PSD_ERROR("NativeFile", "Failed to wait for previous asynchronous read operation.");
        return false;
    }

    return true;
}


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
uint64_t NativeFile::DoGetSize(void) const
{
    [fileHandle synchronizeFile];
    const uint64_t fileSize = [fileHandle seekToEndOfFile];

    return fileSize;
}

PSD_NAMESPACE_END
