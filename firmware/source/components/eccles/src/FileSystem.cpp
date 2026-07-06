//base implementation of ECCLES_FILE_SYSTEM
//PORT NOTE: this whole file replaced arduino's File class and filesystem with LittleFS via esp_vfs_littlefs and POSIX FILE*.
//it is rewritten here on esp-idf's esp_littlefs VFS component (esp_vfs_littlefs_register/unregister)
//plus standard POSIX file calls (fopen/fread/fwrite/fseek/ftell/remove) mounted at "/littlefs/".
//every public method keeps its original name, return type and behaviour.

#include "FileSystem.h"
#include <sys/stat.h>

ECCLES_API {

    //PORT NOTE: arduino's LittleFS.begin(true)/.end()/.exists()/.open() are replaced with
    //esp_vfs_littlefs_register()/esp_vfs_littlefs_unregister() plus plain POSIX calls against the
    //"/littlefs/" mountpoint. we keep the same base path prefixing behaviour the original had
    //(paths not starting with '/' get one prepended) but additionally prefix the esp-idf vfs
    //mountpoint "/littlefs/" itself, since esp-idf (unlike arduino's filesystem wrapper) addresses
    //files through the real vfs path rather than an implicit filesystem-rooted path
    static constexpr e_string LITTLEFS_BASE = "/littlefs";
    static constexpr e_string LITTLEFS_PARTITION_LABEL = "littlefs"; //must match the partition table csv

    e_uint8 FileSystem::u_count = 0;
    e_boolean FileSystem::mounted = false;

    //builds the full vfs path ("/littlefs/" + the user supplied path, adding a leading '/' to
    //the user path if it's missing one, mirroring exactly what the original load() did before
    //handing the path to LittleFS.open())
    static e_boolean buildVfsPath(e_string path,e_char* out,e_uint16 outSize){
        e_char temp[32];
        e_string pt_m = nullptr;

        if(path[0] == '/'){
            pt_m = path;
        } else {
            if(strlen(path) >= sizeof(temp)) return false; //long string without '/'
            temp[0] = '/';
            memcpy(temp + 1,path,strlen(path) + 1);
            pt_m = temp;
        }

        e_int written = snprintf(out,outSize,"%s%s",LITTLEFS_BASE,pt_m);
        return (written > 0) && ((e_uint16)written < outSize);
    }

    FileSystem::FileSystem(){
        //here we mount the littlefs partition
        if(u_count == 0){
        //this is commented out because it causes global initialization error
        //it works on our previous esp version but after we downgrade because of the DAC
        //issue it started crashing on our live test so we default to lazy mounting the filesystem on load

        /*if(!LittleFS.begin()){
            status = FileStatus::MOUNT_FAILED;
            ECCLES_LOG_LINE("unable to mount LittleFs file partition");
            return;
        }
            ECCLES_LOG_LINE("Eccles filesystem mounted successfully");
        }*/
        //if the file system is mounted successfully we list ourself as one of
        //the file system object
        }
        u_count++;
    }

    FileSystem::~FileSystem(){
        //here we remove ourself from the list of filesystem object
        u_count--;

        //we unload all loaded file
        if(file) unload();
        if(u_count == 0){
            //no more file system client we unmount the system here
            if(mounted){
                esp_vfs_littlefs_unregister(LITTLEFS_PARTITION_LABEL);
            }
            mounted = false;
            ECCLES_LOG_LINE("Eccles files system unmounted successfully");
        }
    }

    //this is the entry point of every file operation, before any operation can be done
    //on a file we must first load it, here is the only place we modify status
    FileStatus FileSystem::load(e_string path,e_string mode){
        //mount the filesystem if not mounted yet
        //we ran into a mount bug previously where the system is failing to mount so we
        //format_if_mount_failed = true here, same intent as the original LittleFS.begin(true)
        if(!mounted){
            esp_vfs_littlefs_conf_t conf = {};
            conf.base_path = LITTLEFS_BASE;
            conf.partition_label = LITTLEFS_PARTITION_LABEL;
            //conf.max_files = 5; throws compiler error,not available in this release
            conf.format_if_mount_failed = true;

            esp_err_t res = esp_vfs_littlefs_register(&conf);
            mounted = (res == ESP_OK);
            status = mounted ? FileStatus::SUCCESSFUL : FileStatus::MOUNT_FAILED;
        }
        //we first check if filesystem is actually mounted
        if(status == FileStatus::MOUNT_FAILED) return status;

        //we check for correct mode here
        if(!eccles_compareString(mode,"w") && !eccles_compareString(mode,"r") && !eccles_compareString(mode,"a")){
            return status = FileStatus::BAD_ARG;
        }
        this->mode = mode;

        //checks if the path starts with '/' if not we add it,path must begin with '/' to 
        //be usable on the filesystem, then we add the vfs mountpoint prefix on top of that

        e_char vfsPath[40]; //"/littlefs/" + up to 32 byte path, same overflow guard as the original temp[32]
        if(!buildVfsPath(path,vfsPath,sizeof(vfsPath))){
            ECCLES_LOG_LINE("long path please add /");
            status = FileStatus::BAD_ARG;
            return status;
        }

        //if we got here everything is successful so far
        //we check if we previously loaded a file if so we unload it here

        if(file) unload();

        //now we open our file for operation, PORT NOTE: fopen's mode strings map directly:
        //"r" -> read, "w" -> write+truncate/create, "a" -> append+create, but we always open in
        //binary-safe "+b" combinations aren't needed since posix fopen on esp-idf's vfs is
        //already byte-exact (no text translation), matching arduino's File semantics
        //
        //FIX: this used to open "a" mode as plain "ab". POSIX guarantees that every write to
        //an append-mode stream is repositioned to end-of-file first, *regardless of any fseek
        //that came before it*. write()/insert()/append() below all rely on fseek(...,f_ptr,...)
        //actually landing the write at an arbitrary offset (that's the entire point of
        //insert()), so "ab" silently turned every insert() into a plain append. "r+b" opens
        //an existing file for reading and writing without truncating it, and does honor seeks.
        e_string fMode = eccles_compareString(mode,"r") ? "rb" :
                          eccles_compareString(mode,"w") ? "wb" : "r+b";

        file = fopen(vfsPath,fMode);

        if(!file && eccles_compareString(mode,"a")){
            //"r+b" fails if the file doesn't exist yet; append mode is expected to create the
            //file on first use (as "ab" used to), so fall back to creating it fresh here
            file = fopen(vfsPath,"w+b");
        }

        if(!file){
            //something must be wrong we check if the file even exists
            struct stat st;
            if(stat(vfsPath,&st) != 0) return (status = FileStatus::FILE_DOESNT_EXIST);
            
            //what could be wrong we mounted the system at constructor and checked it before now
            return (status = FileStatus::ERROR);
        }
        f_ptr = 0; //fresh file handle starts at position 0, mirrors arduino File's initial cursor
        //we are good to go,everything is normal
        return (status = FileStatus::SUCCESSFUL);

    }

    //gets the size of the file we loaded previously with load we avoid saving this in a variable
    //for a reason best known to us
    e_uint32 FileSystem::getSize(){
        //we first check if the file is actually loaded
        if(file && status == FileStatus::SUCCESSFUL){
            //PORT NOTE: posix has no direct "size of open FILE*" call like arduino's file.size(),
            //so we save the current position, seek to the end, read that as the size, then seek
            //back, this leaves the file's cursor exactly as it was before this call, same as the
            //original which also didn't disturb f_ptr (file.size() doesn't move arduino's cursor)
            long cur = ftell(file);
            fseek(file,0,SEEK_END);
            long sz = ftell(file);
            fseek(file,cur,SEEK_SET);
            return (e_uint32) sz;
        }
        return 0;
    }

    //reads from the file in chunks this updates the file index so calls after this reads the remaining
    //bytes instead of starting afresh,to start afresh again first set FileSystem.f_ptr = 0 to 0

    FileStatus FileSystem::read(e_uint8* buffer,e_uint16 len){
        //we use e_uint16 here because in this mode we are not assuming to read past 65,635 in one reading

        //we first check if the file is correctly loaded
        if(!file || status != FileStatus::SUCCESSFUL){
            ECCLES_LOG_LINE("unable to read from file:"); //we don't store the file path and we cant get it from file eighter since it may be null
            return status;
        }

        //file is correctly loaded we continue here,we check if the mode is actually for reading
        if(!eccles_compareString(mode,"r")) return FileStatus::BAD_MODE;

        //we check if the area we are asked to read is actually withing the file bound
        //FIX: was ">=", which incorrectly rejected a read that lands exactly on the last
        //byte of the file (f_ptr + len == getSize() is a perfectly valid, complete read)
        if(f_ptr + len > getSize()) return FileStatus::END_OF_FILE; //we are trying to read past the file size
        if(len > FILE_MAX_CHUNK_BUFFER){
            ECCLES_LOG_LINE("reading too much bytes will overfload ram are you sure it is what you want");
            return FileStatus::BAD_ARG;
        }

        //if we got here everything is ok,we fill in the buffer
        //we restart the file cursor and set it to f_ptr
        if(fseek(file,0,SEEK_SET) != 0 || fseek(file,(long)f_ptr,SEEK_SET) != 0){
            ECCLES_LOG_LINE("unable to update file cursor pointer: read failed");
            return FileStatus::ERROR;
        }
        
        //check if file reads exactly the amount we requested
        e_uint16 s = (e_uint16) fread(buffer,1,len,file);
        if(s != len){
            ECCLES_LOG_LINE("corrupted file or EOF reached");
            
            //we update the f_ptr anyway incase it is a system error
            f_ptr += s;
            return FileStatus::END_OF_FILE;
        }

        //everything is perfect 
        f_ptr += s;
        return FileStatus::SUCCESSFUL;
    }

    //reads the entire file regarding of the size,this is not recommended but it may be neccasary at times
    //the provided buffer given here must be less than size as returned by getSize(),so make sure to 
    //call getSize() to obtain the actual file size
    FileStatus FileSystem::readAll(e_uint8 * buffer,e_uint32 len){
        //we first check if file is loaded
        if(!file || status != FileStatus::SUCCESSFUL){
            //file load failed since status can only be changed by load
            ECCLES_LOG_LINE("attempting to read from an invalid or unloaded file");
            return status;
        }

        //we check that mode is in read
        if(!eccles_compareString(mode,"r")) return FileStatus::BAD_MODE;

        //check if the length we are told to read is within the file bound
        if(len > getSize()){
            ECCLES_LOG_LINE("file buffer bigger file size");
            return FileStatus::BAD_ARG;
        }

        //since read all does't affect f_ptr we avoid it here
        fseek(file,0,SEEK_SET); //resets file cursor
        if(fread(buffer,1,len,file) != len){
            //if we are not able to get up to the amount requested possible we have reached EOF
            return FileStatus::BAD_ARG;
        }

        //all perfect we are good to go
        return FileStatus::SUCCESSFUL;
    }

    //here we read the file in chunk starting from the offset provided and we read the amount provided
    //in len,this behaves like a random access file in practice
    FileStatus FileSystem::chunk(e_uint8* buffer,e_uint32 offset,e_uint16 len){
        //we save the f_ptr for resetting later
        e_uint32 t_ptr = f_ptr; 
        f_ptr = offset;
        FileStatus f = read(buffer,len);

        //resetting f_ptr;
        f_ptr = t_ptr;
        return f;
    }

    //this changes the f_ptr to the set value
    FileStatus FileSystem::skip(e_uint32 len){
        if(len > getSize()){
            ECCLES_LOG_LINE("attempting to skip past the file size");
            return FileStatus::BAD_ARG;
        }
        f_ptr = len;
        return FileStatus::SUCCESSFUL;
    }

    //this checks if this file is valid,which means the file is actualy loaded
    e_boolean FileSystem::exists() const {
        return (file && status == FileStatus::SUCCESSFUL);
    }

    //this operator allows us to check the validity of the file system on if
    FileSystem::operator bool() const{
        return exists();
    }

    //write to the loaded file,the file must be loaded on write mode and this function
    //overwrite everything already written to the file, to add to the file use append
    //append is recommended for large files to be written in chunks
    FileStatus FileSystem::write(e_uint8* buffer,e_uint32 len){
        //we check that the file is actually loaded successfully
        if(!exists()){
            ECCLES_LOG_LINE("attempting to write to an invalid or unloaded file");
            return status;
        }

        //check if the mode is valid
        if(!eccles_compareString(mode,"w") && !eccles_compareString(mode,"a")){
            ECCLES_LOG_LINE("attempting to write to a file thats not in write mode");
            return FileStatus::BAD_MODE;
        }

        //if append we append
        if(eccles_compareString(mode,"a")){
            //append mode we seek to f_ptr
            fseek(file,0,SEEK_SET); //resets file cursor
            fseek(file,(long)f_ptr,SEEK_SET); //we sets cursor to the write position
            if(fwrite(buffer,1,len,file) != len){
                ECCLES_LOG_LINE("failed to write the complete buffer");
                //this should't happen maybe memory error
                return FileStatus::ERROR;
            }
            fflush(file); //PORT NOTE: posix streams are buffered, arduino's File::write flushed
                           //implicitly to the underlying flash-backed handle, so we flush here too
            return FileStatus::SUCCESSFUL;
        } else {
            //we are write mode we overwrite everything here
            fseek(file,0,SEEK_SET); //resets file cursor
            if(fwrite(buffer,1,len,file) != len){
                //memory error how can this be,we raise alarm
                ECCLES_LOG_LINE("unable to write the complete buffer maybe memory error");
                return FileStatus::ERROR;
            }
            fflush(file);
            return FileStatus::SUCCESSFUL;
        }

        //we should't get here,but if we do we report error
        return FileStatus::ERROR; //why are we here, should't be
    }

    //returns the status text useful for logging
    e_string FileSystem::getStatusText(FileStatus s){
        switch (s)
        {
        case FileStatus::BAD_ARG : return "Bad argument";
        case FileStatus::BAD_MODE : return "Bad file mode";
        case FileStatus::END_OF_FILE : return "end of file";
        case FileStatus::ERROR : return "error";
        case FileStatus::FILE_DOESNT_EXIST : return "file doest exist";
        case FileStatus::MOUNT_FAILED : return "mount failed";
        case FileStatus::SUCCESSFUL : return "successful";
        
        default: return "unknown file status";
        }
    }

    //this adds bytes to the end of the file but be sure the file is opened on append mode
    FileStatus FileSystem::append(e_uint8* buffer,e_uint32 len){
        //here mode must be on append only to avoid unnecessary confusion
        if(!eccles_compareString(mode,"a")){
            ECCLES_LOG_LINE("attempting to append to a file that is not appendable");
            return FileStatus::BAD_MODE;
        }

        //we are cool we write to it here
        e_uint32 t_ptr = f_ptr; //we store this for insert modes
        f_ptr = getSize(); //we are writing to the end of the file
        FileStatus s = write(buffer,len);
        f_ptr = t_ptr; //we restore file cursor pointer incase its needed for inser
        return s;
    }

    //insert to a file must be opened with mode a
    FileStatus FileSystem::insert(e_uint8* buffer,e_uint32 offset,e_uint32 len){
        //we checks if the mode is appendable
        if(!eccles_compareString(mode,"a")){
            //bad idea whats going on
            ECCLES_LOG_LINE("attempting to insert in a non appendable files please load the file in mode a");
            return FileStatus::BAD_MODE;
        }

        //if the offset is specified we use it if not we use f_ptr
        //if offset is set f_ptr is unchanged but if not we change it to the current last read pos
        
        e_uint32 t_ptr = f_ptr; //storing file cursor for restore
        f_ptr = offset ? offset : t_ptr;

        FileStatus s = write(buffer,len);
        f_ptr = offset ? offset + len : t_ptr;
        return s;
    }

    //return the underlying file handle, PORT NOTE: returns FILE* instead of arduino's File&
    FILE* FileSystem::getFile(){
        return file;
    }

    //here we remove this file from memory,after this all operation this filesystem 
    //becomes invalid
    FileStatus FileSystem::unload(){
        //we close the file first
        if(file){
            fclose(file);
            file = nullptr; //resets the file handle to default
        }
        return FileStatus::SUCCESSFUL;
    }
};
