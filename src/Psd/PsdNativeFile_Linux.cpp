
#include "PsdPch.h"

#include "PsdLog.h"
#include "PsdFile.h"
#include "PsdAllocator.h"
#include "PsdNamespace.h"
#include "PsdMemoryUtil.h"
#include "PsdNativeFile_Linux.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <aio.h>

#include <cstring>
#include <cstdlib>
#include <cwchar>
#include <string>



PSD_NAMESPACE_BEGIN
/**
 * @brief Convert wchar_t * to char *
 * 
 * @param ws 
 * @return char * 
 */
static char *convert_ws(const wchar_t *ws){
    char *buffer;
    size_t n = std::wcslen(ws) * 4 + 1;
    buffer = static_cast<char*>(std::malloc(n));
    std::memset(buffer,0,n);
    if(buffer == nullptr){
        return nullptr;
    }
    std::wcstombs(buffer,ws,n);
    return buffer;
}

NativeFile::NativeFile(Allocator *alloc):
    File(alloc),
    fd(-1){}

//Convert wchar to char and open

bool NativeFile::DoOpenRead(const wchar_t* filename){
    char *name = convert_ws(filename);
    fd = open(name,O_RDONLY);
    if(fd == -1){
        PSD_ERROR("NativeFile","open(%s) => %s",name,strerror(errno));
        std::free(name);
        return false;
    }
    std::free(name);
    return true;
}
bool NativeFile::DoOpenWrite(const wchar_t* filename){
    char *name = convert_ws(filename);
    //Create a new file
    fd = open(name,O_WRONLY | O_CREAT,S_IRUSR | S_IWUSR);
    if(fd == -1){
        PSD_ERROR("NativeFile","open(%s) => %s",name,strerror(errno));
        std::free(name);
        return false;
    }
    std::free(name);
    return true;
}
bool NativeFile::DoClose(){
    int ret = close(fd);
    fd = -1;
    return ret == 0;
}

//Wrtie / Read

File::ReadOperation NativeFile::DoRead(void* buffer, uint32_t count, uint64_t position){
    aiocb *operation = memoryUtil::Allocate<aiocb>(m_allocator);
    std::memset(operation,0,sizeof(aiocb));

    operation->aio_buf = buffer;
    operation->aio_fildes = fd;
    operation->aio_lio_opcode = LIO_READ;//Do read
    operation->aio_nbytes = count;
    operation->aio_offset = position;
    operation->aio_reqprio = 0;
    operation->aio_sigevent.sigev_notify = SIGEV_NONE;//No signal will be send 

    //OK Execute it
    if(aio_read(operation) == -1){
        //Has Error
        PSD_ERROR("NativeFile","On DoRead aio_read() => %s",strerror(errno));
        memoryUtil::Free(m_allocator,operation);
        return nullptr;
    }
    return operation;
}
File::ReadOperation NativeFile::DoWrite(const void* buffer, uint32_t count, uint64_t position){
    aiocb *operation = memoryUtil::Allocate<aiocb>(m_allocator);
    std::memset(operation,0,sizeof(aiocb));
    
    operation->aio_buf = const_cast<void*>(buffer);
    operation->aio_fildes = fd;
    operation->aio_lio_opcode = LIO_WRITE;//Do Write
    operation->aio_nbytes = count;
    operation->aio_offset = position;
    operation->aio_reqprio = 0;
    operation->aio_sigevent.sigev_notify = SIGEV_NONE;//No signal will be send 

    //OK Execute it
    if(aio_read(operation) == -1){
        //Has Error
        PSD_ERROR("NativeFile","On DoWrite aio_read() => %s",strerror(errno));
        memoryUtil::Free(m_allocator,operation);
        return nullptr;
    }
    return operation;
}

//Wait for R/W

static bool generic_wait(aiocb *operation,Allocator *alloc){
    //Wait for it
    if(aio_suspend(&operation,1,nullptr) == -1){
        PSD_ERROR("NativeFile","aio_suspend() => %s",strerror(errno));
        memoryUtil::Free(alloc,operation);
        return false;
    }
    //Get status
    ssize_t ret = aio_return(operation);
    memoryUtil::Free(alloc,operation);
    return ret != -1;
}

bool NativeFile::DoWaitForRead(ReadOperation &_operation){
    aiocb *operation = static_cast<aiocb*>(_operation);
    return generic_wait(operation,m_allocator);
}
bool NativeFile::DoWaitForWrite(ReadOperation &_operation){
    aiocb *operation = static_cast<aiocb*>(_operation);
    return generic_wait(operation,m_allocator);
}


uint64_t NativeFile::DoGetSize() const{
    struct stat s;
    if(fstat(fd,&s) == -1){
        PSD_ERROR("NativeFile","lstat() => %s",strerror(errno));
        //Emm,return 0 on error
        return 0;
    }
    return s.st_size;
}

PSD_NAMESPACE_END