// Windows/FileDir.cpp

#include "StdAfx.h"

#include "FileDir.h"
#include "FileName.h"
#include "FileFind.h"
#include "Defs.h"
#ifndef _UNICODE
#include "../Common/StringConvert.h"
#endif

#define NEED_NAME_WINDOWS_TO_UNIX
#include "myPrivate.h"

#include <unistd.h> // rmdir
#include <errno.h>

#include <sys/stat.h> // mkdir
#include <sys/types.h>
#include <fcntl.h>

#include <utime.h>

// #define TRACEN(u) u;
#define TRACEN(u)  /* */

#ifdef HAVE_LSTAT
extern int global_use_lstat;
#endif

extern BOOLEAN WINAPI RtlTimeToSecondsSince1970( const LARGE_INTEGER *Time, DWORD *Seconds );

DWORD WINAPI GetFullPathName( LPCSTR name, DWORD len, LPSTR buffer, LPSTR *lastpart ) {
  if (name == 0) return 0;

  DWORD name_len = strlen(name);

  if (name[0] == '/') {
    DWORD ret = name_len+2;
    if (ret >= len) {
      TRACEN((printf("GetFullPathNameA(%s,%d,)=0000 (case 0)\n",name, (int)len)))
      return 0;
    }
    strcpy(buffer,"c:");
    strcat(buffer,name);

    *lastpart=buffer;
    char *ptr=buffer;
    while (*ptr) {
      if (*ptr == '/')
        *lastpart=ptr+1;
      ptr++;
    }
    TRACEN((printf("GetFullPathNameA(%s,%d,%s,%s)=%d\n",name, (int)len,buffer, *lastpart,(int)ret)))
    return ret;
  }
  if (isascii(name[0]) && (name[1] == ':')) {
    DWORD ret = name_len;
    if (ret >= len) {
      TRACEN((printf("GetFullPathNameA(%s,%d,)=0000 (case 1)\n",name, (int)len)))
      return 0;
    }
    strcpy(buffer,name);

    *lastpart=buffer;
    char *ptr=buffer;
    while (*ptr) {
      if (*ptr == '/')
        *lastpart=ptr+1;
      ptr++;
    }
    TRACEN((printf("GetFullPathNameA(%s,%d,%s,%s)=%d\n",name, (int)len,buffer, *lastpart,(int)ret)))
    return ret;
  }

  // name is a relative pathname.
  //
  if (len < 2) {
    TRACEN((printf("GetFullPathNameA(%s,%d,)=0000 (case 2)\n",name, (int)len)))
    return 0;
  }

  DWORD ret = 0;
  char begin[MAX_PATHNAME_LEN];
  /* DWORD begin_len = GetCurrentDirectoryA(MAX_PATHNAME_LEN,begin); */
  DWORD begin_len = 0;
  begin[0]='c';
  begin[1]=':';
  char * cret = getcwd(begin+2, MAX_PATHNAME_LEN - 3);
  if (cret) {
    begin_len = strlen(begin);
  }
   
  if (begin_len >= 1) {
    //    strlen(begin) + strlen("/") + strlen(name)
    ret = begin_len     +    1        + name_len;

    if (ret >= len) {
      TRACEN((printf("GetFullPathNameA(%s,%d,)=0000 (case 4)\n",name, (int)len)))
      return 0;
    }
    strcpy(buffer,begin);
    strcat(buffer,"/");
    strcat(buffer,name);

    *lastpart=buffer + begin_len + 1;
    char *ptr=buffer;
    while (*ptr) {
      if (*ptr == '/')
        *lastpart=ptr+1;
      ptr++;
    }
    TRACEN((printf("GetFullPathNameA(%s,%d,%s,%s)=%d\n",name, (int)len,buffer, *lastpart,(int)ret)))
  } else {
    ret = 0;
    TRACEN((printf("GetFullPathNameA(%s,%d,)=0000 (case 5)\n",name, (int)len)))
  }
  return ret;
}

static BOOL WINAPI RemoveDirectory(LPCSTR path) {
  if (!path || !*path) {
    SetLastError(ERROR_PATH_NOT_FOUND);
    return FALSE;
  }
  const char * name = nameWindowToUnix(path);
  TRACEN((printf("RemoveDirectoryA(%s)\n",name)))

  if (rmdir( name ) != 0) {
    return FALSE;
  }
  return TRUE;
}

static int copy_fd(int fin,int fout)
{
  char buffer[16384];
  ssize_t ret_in;
  ssize_t ret_out;

  do {
    ret_out = -1;
    do {
      ret_in = read(fin, buffer,sizeof(buffer));
    } while (ret_in < 0 && (errno == EINTR));
    if (ret_in >= 1) {
      do {
        ret_out = write (fout, buffer, ret_in);
      } while (ret_out < 0 && (errno == EINTR));
    } else if (ret_in == 0) {
      ret_out = 0;
    }
  } while (ret_out >= 1);
  return ret_out;
}

static BOOL CopyFile(const char *src,const char *dst)
{
  int ret = -1;

#ifdef O_BINARY
  int   flags = O_BINARY;
#else
  int   flags = 0;
#endif

#ifdef O_LARGEFILE
  flags |= O_LARGEFILE;
#endif

  int fout = open(dst,O_CREAT | O_WRONLY | O_EXCL | flags, 0600);
  if (fout != -1)
  {
    int fin = open(src,O_RDONLY | flags , 0600);
    if (fin != -1)
    {
      ret = copy_fd(fin,fout);
      if (ret == 0) ret = close(fin);
      else                close(fin);
    }
    if (ret == 0) ret = close(fout);
    else                close(fout);
  }
  if (ret == 0) return TRUE;
  return FALSE;
}

bool MyMoveFile( LPCTSTR fn1, LPCTSTR fn2 ) {
  const char * src = nameWindowToUnix(fn1);
  const char * dst = nameWindowToUnix(fn2);

  TRACEN((printf("MoveFileW(%s,%s)\n",src,dst)))

  int ret = rename(src,dst);
  if (ret != 0)
  {
    if (errno == EXDEV) // FIXED : bug #1112167 (Temporary directory must be on same partition as target)
    {
      BOOL bret = CopyFile(src,dst);
      if (bret == FALSE) return false;

      struct stat info_file;
      ret = stat(src,&info_file);
      if (ret == 0) {
        ret = chmod(dst,info_file.st_mode);
      }
      if (ret == 0) {
         ret = unlink(src);
      }
      if (ret == 0) return true;
    }
    return false;
  }
  return true;
}

/*****************************************************************************************/


namespace NWindows {
namespace NFile {
namespace NDirectory {

bool MyRemoveDirectory(LPCTSTR pathName)
{
  return BOOLToBool(::RemoveDirectory(pathName));
}

bool SetDirTime(LPCWSTR fileName, const FILETIME * /* creationTime */ ,
      const FILETIME *lpLastAccessTime, const FILETIME *lpLastWriteTime)
{
  AString  cfilename = UnicodeStringToMultiByte(fileName);
  const char * unix_filename = nameWindowToUnix((const char *)cfilename);

  struct utimbuf buf;

  struct stat    oldbuf;
  int ret = stat(unix_filename,&oldbuf);
  if (ret == 0) {
    buf.actime  = oldbuf.st_atime;
    buf.modtime = oldbuf.st_mtime;
  } else {
    time_t current_time = time(0);
    buf.actime  = current_time;
    buf.modtime = current_time;
  }

  if (lpLastAccessTime)
  {
    LARGE_INTEGER  ltime;
    DWORD dw;
    ltime.QuadPart = lpLastAccessTime->dwHighDateTime;
    ltime.QuadPart = (ltime.QuadPart << 32) | lpLastAccessTime->dwLowDateTime;
    RtlTimeToSecondsSince1970( &ltime, &dw );
    buf.actime = dw;
  }

  if (lpLastWriteTime)
  {
    LARGE_INTEGER  ltime;
    DWORD dw;
    ltime.QuadPart = lpLastWriteTime->dwHighDateTime;
    ltime.QuadPart = (ltime.QuadPart << 32) | lpLastWriteTime->dwLowDateTime;
    RtlTimeToSecondsSince1970( &ltime, &dw );
    buf.modtime = dw;
  }

  /* ret = */ utime(unix_filename, &buf);

  return true;
}

#ifndef _UNICODE
bool MySetFileAttributes(LPCWSTR fileName, DWORD fileAttributes)
{  
  return MySetFileAttributes(UnicodeStringToMultiByte(fileName, CP_ACP), fileAttributes);
}

bool MyRemoveDirectory(LPCWSTR pathName)
{  
  return MyRemoveDirectory(UnicodeStringToMultiByte(pathName, CP_ACP));
}

bool MyMoveFile(LPCWSTR existFileName, LPCWSTR newFileName)
{  
  UINT codePage = CP_ACP;
  return ::MyMoveFile(UnicodeStringToMultiByte(existFileName, codePage),
      UnicodeStringToMultiByte(newFileName, codePage));
}
#endif


static int convert_to_symlink(const char * name) {
  FILE *file = fopen(name,"rb");
  if (file) {
    char buf[MAX_PATHNAME_LEN+1];
    char * ret = fgets(buf,sizeof(buf)-1,file);
    fclose(file);
    if (ret) {
      int ir = unlink(name);
      if (ir == 0) {
        ir = symlink(buf,name);
      }
      return ir;    
    }
  }
  return -1;
}

bool MySetFileAttributes(LPCTSTR fileName, DWORD fileAttributes)
{
  if (!fileName) {
    SetLastError(ERROR_PATH_NOT_FOUND);
    TRACEN((printf("MySetFileAttributes(NULL,%d) : false-1\n",fileAttributes)))
    return false;
  }
  const char * name = nameWindowToUnix(fileName);
  struct stat stat_info;
#ifdef HAVE_LSTAT
  if (global_use_lstat) {
    if(lstat(name,&stat_info)!=0) {
      TRACEN((printf("MySetFileAttributes(%s,%d) : false-2-1\n",name,fileAttributes)))
      return false;
    }
  } else
#endif
  {
    if(stat(name,&stat_info)!=0) {
      TRACEN((printf("MySetFileAttributes(%s,%d) : false-2-2\n",name,fileAttributes)))
      return false;
    }
  }

  if (fileAttributes & FILE_ATTRIBUTE_UNIX_EXTENSION) {
     stat_info.st_mode = fileAttributes >> 16;
#ifdef HAVE_LSTAT
     if (S_ISLNK(stat_info.st_mode)) {
        if ( convert_to_symlink(name) != 0) {
          TRACEN((printf("MySetFileAttributes(%s,%d) : false-3\n",name,fileAttributes)))
          return false;
        }
     } else
#endif
     if (S_ISREG(stat_info.st_mode)) {
       chmod(name,stat_info.st_mode);
     } else if (S_ISDIR(stat_info.st_mode)) {
       // user/7za must be able to create files in this directory
       stat_info.st_mode |= (S_IRUSR | S_IWUSR | S_IXUSR);
       chmod(name,stat_info.st_mode);
     }
#ifdef HAVE_LSTAT
  } else if (!S_ISLNK(stat_info.st_mode)) {
    // do not use chmod on a link
#else
  } else {
#endif
    if (fileAttributes & FILE_ATTRIBUTE_READONLY) {
      if(!S_ISDIR(stat_info.st_mode)) {
        /* FILE_ATTRIBUTE_READONLY ignored for directory. */
        stat_info.st_mode &= ~0222; /* octal!, clear write permission bits */
      }
    } else {
      /* add write permission */
      stat_info.st_mode |= (0600 | ((stat_info.st_mode & 044) >> 1)) ;
    }

    chmod(name,stat_info.st_mode);
  }
  TRACEN((printf("MySetFileAttributes(%s,%d) : true\n",name,fileAttributes)))

  return true;
}

bool MyCreateDirectory(LPCTSTR pathName)
{  
  if (!pathName || !*pathName) {
    SetLastError(ERROR_PATH_NOT_FOUND);
    return false;
  }

  const char * name = nameWindowToUnix(pathName);
  bool bret = false;
  if (mkdir( name, 0700 ) == 0) bret = true;

  TRACEN((printf("MyCreateDirectory(%s)=%d\n",name,(int)bret)))
  return bret;
}

#ifndef _UNICODE
bool MyCreateDirectory(LPCWSTR pathName)
{  
  return MyCreateDirectory(UnicodeStringToMultiByte(pathName, CP_ACP));
}
#endif

bool CreateComplexDirectory(LPCTSTR _aPathName)
{
  CSysString pathName = _aPathName;
  int pos = pathName.ReverseFind(TEXT(CHAR_PATH_SEPARATOR));
  if (pos > 0 && pos == pathName.Length() - 1)
  {
    if (pathName.Length() == 3 && pathName[1] == ':')
      return true; // Disk folder;
    pathName.Delete(pos);
  }
  CSysString pathName2 = pathName;
  pos = pathName.Length();
  while(true)
  {
    if(MyCreateDirectory(pathName))
      break;
    if(::GetLastError() == ERROR_ALREADY_EXISTS)
    {
      NFind::CFileInfo fileInfo;
      if (!NFind::FindFile(pathName, fileInfo)) // For network folders
        return true;
      if (!fileInfo.IsDirectory())
        return false;
      break;
    }
    pos = pathName.ReverseFind(TEXT(CHAR_PATH_SEPARATOR));
    if (pos < 0 || pos == 0)
      return false;
    if (pathName[pos - 1] == ':')
      return false;
    pathName = pathName.Left(pos);
  }
  pathName = pathName2;
  while(pos < pathName.Length())
  {
    pos = pathName.Find(TEXT(CHAR_PATH_SEPARATOR), pos + 1);
    if (pos < 0)
      pos = pathName.Length();
    if(!MyCreateDirectory(pathName.Left(pos)))
      return false;
  }
  return true;
}

#ifndef _UNICODE

bool CreateComplexDirectory(LPCWSTR _aPathName)
{
  UString pathName = _aPathName;
  int pos = pathName.ReverseFind(WCHAR_PATH_SEPARATOR);
  if (pos > 0 && pos == pathName.Length() - 1)
  {
    if (pathName.Length() == 3 && pathName[1] == L':')
      return true; // Disk folder;
    pathName.Delete(pos);
  }
  UString pathName2 = pathName;
  pos = pathName.Length();
  while(true)
  {
    if(MyCreateDirectory(pathName))
      break;
    if(::GetLastError() == ERROR_ALREADY_EXISTS)
    {
      NFind::CFileInfoW fileInfo;
      if (!NFind::FindFile(pathName, fileInfo)) // For network folders
        return true;
      if (!fileInfo.IsDirectory())
        return false;
      break;
    }
    pos = pathName.ReverseFind(WCHAR_PATH_SEPARATOR);
    if (pos < 0 || pos == 0)
      return false;
    if (pathName[pos - 1] == L':')
      return false;
    pathName = pathName.Left(pos);
  }
  pathName = pathName2;
  while(pos < pathName.Length())
  {
    pos = pathName.Find(WCHAR_PATH_SEPARATOR, pos + 1);
    if (pos < 0)
      pos = pathName.Length();
    if(!MyCreateDirectory(pathName.Left(pos)))
      return false;
  }
  return true;
}

#endif

bool DeleteFileAlways(LPCTSTR name)
{
  if (!name || !*name) {
    SetLastError(ERROR_PATH_NOT_FOUND);
    return false;
  }
   const char * unixname = nameWindowToUnix(name);
   bool bret = false;
   if (remove(unixname) == 0) bret = true;
   TRACEN((printf("DeleteFileAlways(%s)=%d\n",unixname,(int)bret)))
   return bret;
}

#ifndef _UNICODE
bool DeleteFileAlways(LPCWSTR name)
{  
  return DeleteFileAlways(UnicodeStringToMultiByte(name, CP_ACP));
}
#endif

static bool RemoveDirectorySubItems2(const CSysString pathPrefix,
    const NFind::CFileInfo &fileInfo)
{
  if(fileInfo.IsDirectory())
    return RemoveDirectoryWithSubItems(pathPrefix + fileInfo.Name);
  else
    return DeleteFileAlways(pathPrefix + fileInfo.Name);
}

bool RemoveDirectoryWithSubItems(const CSysString &path)
{
  NFind::CFileInfo fileInfo;
  CSysString pathPrefix = path + NName::kDirDelimiter;
  {
    NFind::CEnumerator enumerator(pathPrefix + TCHAR(NName::kAnyStringWildcard));
    while(enumerator.Next(fileInfo))
      if(!RemoveDirectorySubItems2(pathPrefix, fileInfo))
        return false;
  }
  return BOOLToBool(::RemoveDirectory(path));
}

#ifndef _UNICODE
static bool RemoveDirectorySubItems2(const UString pathPrefix,
    const NFind::CFileInfoW &fileInfo)
{
  if(fileInfo.IsDirectory())
    return RemoveDirectoryWithSubItems(pathPrefix + fileInfo.Name);
  else
    return DeleteFileAlways(pathPrefix + fileInfo.Name);
}
bool RemoveDirectoryWithSubItems(const UString &path)
{
  NFind::CFileInfoW fileInfo;
  UString pathPrefix = path + UString(NName::kDirDelimiter);
  {
    NFind::CEnumeratorW enumerator(pathPrefix + UString(NName::kAnyStringWildcard));
    while(enumerator.Next(fileInfo))
      if(!RemoveDirectorySubItems2(pathPrefix, fileInfo))
        return false;
  }
  return MyRemoveDirectory(path);
}
#endif

#ifndef _WIN32_WCE

bool MyGetFullPathName(LPCTSTR fileName, CSysString &resultPath, 
    int &fileNamePartStartIndex)
{
  LPTSTR fileNamePointer = 0;
  LPTSTR buffer = resultPath.GetBuffer(MAX_PATH);
  DWORD needLength = ::GetFullPathName(fileName, MAX_PATH + 1, 
      buffer, &fileNamePointer);
  resultPath.ReleaseBuffer();
  if (needLength == 0 || needLength >= MAX_PATH)
    return false;
  if (fileNamePointer == 0)
    fileNamePartStartIndex = lstrlen(fileName);
  else
    fileNamePartStartIndex = fileNamePointer - buffer;
  return true;
}

#ifndef _UNICODE
bool MyGetFullPathName(LPCWSTR fileName, UString &resultPath, 
    int &fileNamePartStartIndex)
{
    const UINT currentPage = CP_ACP;
    CSysString sysPath;
    if (!MyGetFullPathName(UnicodeStringToMultiByte(fileName, 
        currentPage), sysPath, fileNamePartStartIndex))
      return false;
    UString resultPath1 = MultiByteToUnicodeString(
        sysPath.Left(fileNamePartStartIndex), currentPage);
    UString resultPath2 = MultiByteToUnicodeString(
        sysPath.Mid(fileNamePartStartIndex), currentPage);
    fileNamePartStartIndex = resultPath1.Length();
    resultPath = resultPath1 + resultPath2;
    return true;
}
#endif


bool MyGetFullPathName(LPCTSTR fileName, CSysString &path)
{
  int index;
  return MyGetFullPathName(fileName, path, index);
}

#ifndef _UNICODE
bool MyGetFullPathName(LPCWSTR fileName, UString &path)
{
  int index;
  return MyGetFullPathName(fileName, path, index);
}
#endif

#endif

static DWORD mySearchPathA( LPCSTR path, LPCSTR name, LPCSTR ext,
                            DWORD buflen, LPSTR buffer, LPSTR *lastpart ) {
  if (path != 0) {
    printf("NOT EXPECTED : SearchPathA : path != NULL\n");
    exit(EXIT_FAILURE);
  }

  if (ext != 0) {
    printf("NOT EXPECTED : SearchPathA : ext != NULL\n");
    exit(EXIT_FAILURE);
  }

  const char *p7zip_home_dir = getenv("P7ZIP_HOME_DIR");
  if (p7zip_home_dir) {
    AString dir_path = p7zip_home_dir;
    dir_path += name;

    TRACEN((printf("mySearchPathA() fopen-2(%s)\n",(const char *)dir_path)))
    FILE *file = fopen((const char *)dir_path,"r");
    if (file) {
      DWORD ret = strlen((const char *)dir_path);
      fclose(file);
      if (ret >= buflen) {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return 0;
      }
      strcpy(buffer,(const char *)dir_path);
      if (lastpart)
        *lastpart = buffer + strlen(p7zip_home_dir);

      return ret;
    }
  }

  return 0;
}

/* needed to find .DLL/.so and SFX */
static DWORD SearchPathA( LPCSTR path, LPCSTR name, LPCSTR ext,
                   DWORD buflen, LPSTR buffer, LPSTR *lastpart ) {
  if (buffer == 0) {
    printf("NOT EXPECTED : SearchPathA : buffer == NULL\n");
    exit(EXIT_FAILURE);
  }
  *buffer=0;
  DWORD ret = mySearchPathA(path,name,ext,buflen,buffer,lastpart);

  TRACEN((printf("SearchPathA(%s,%s,%s,%d,%s,%p)=%d\n",
                 (path ? path : "<NULL>"),
                 (name ? name : "<NULL>"),
                 (ext ? ext : "<NULL>"),
                 (int)buflen,
                 buffer,lastpart,(int)ret)));
  return ret;
}

bool MySearchPath(LPCTSTR path, LPCTSTR fileName, LPCTSTR extension, 
  CSysString &resultPath, UInt32 &filePart)
{
  LPTSTR filePartPointer;
  DWORD value = SearchPathA(path, fileName, extension, 
    MAX_PATH, resultPath.GetBuffer(MAX_PATH), &filePartPointer);
  filePart = filePartPointer - (LPCTSTR)resultPath;
  resultPath.ReleaseBuffer();
  if (value == 0 || value > MAX_PATH)
    return false;
  return true;
}

#ifndef _UNICODE
bool MySearchPath(LPCWSTR path, LPCWSTR fileName, LPCWSTR extension, 
  UString &resultPath, UInt32 &filePart)
{
  const UINT currentPage = CP_ACP;
  CSysString sysPath;
  if (!MySearchPath(
      path != 0 ? (LPCTSTR)UnicodeStringToMultiByte(path, currentPage): 0,
      fileName != 0 ? (LPCTSTR)UnicodeStringToMultiByte(fileName, currentPage): 0,
      extension != 0 ? (LPCTSTR)UnicodeStringToMultiByte(extension, currentPage): 0,
      sysPath, filePart))
    return false;
  UString resultPath1 = MultiByteToUnicodeString(
    sysPath.Left(filePart), currentPage);
  UString resultPath2 = MultiByteToUnicodeString(
    sysPath.Mid(filePart), currentPage);
  filePart = resultPath1.Length();
  resultPath = resultPath1 + resultPath2;
  return true;
}
#endif


bool MyGetTempPath(CSysString &path)
{
  path = "c:/tmp/"; // final '/' is needed
  return true;
}


#ifndef _UNICODE
bool MyGetTempPath(UString &path)
{
    CSysString sysPath;
    if (!MyGetTempPath(sysPath))
      return false;
    path = MultiByteToUnicodeString(sysPath, CP_ACP);
    return true;
}
#endif


UINT MyGetTempFileName(LPCTSTR dirPath, LPCTSTR prefix, CSysString &path)
{
/* UINT number = ::GetTempFileName(dirPath, prefix, 0, path.GetBuffer(MAX_PATH)); */
  UINT number = (UINT)getpid();
  sprintf(path.GetBuffer(MAX_PATH),"%s%s%d.tmp",dirPath,prefix,(int)number);
  path.ReleaseBuffer();
  TRACEN((printf("GetTempFileNameA(%s,%s,0,%s)=%d\n",dirPath,prefix,(const char *)path,(int)number)))
  return number;
}

#ifndef _UNICODE
UINT MyGetTempFileName(LPCWSTR dirPath, LPCWSTR prefix, UString &path)
{
      const UINT currentPage = CP_ACP;
      CSysString sysPath;
      UINT number = MyGetTempFileName(
          dirPath ? (LPCTSTR)UnicodeStringToMultiByte(dirPath, currentPage): 0, 
          prefix ?  (LPCTSTR)UnicodeStringToMultiByte(prefix, currentPage): 0, 
          sysPath);
      path = MultiByteToUnicodeString(sysPath, currentPage);
  return number;
}
#endif

UINT CTempFile::Create(LPCTSTR dirPath, LPCTSTR prefix, CSysString &resultPath)
{
  Remove();
  UINT number = MyGetTempFileName(dirPath, prefix, resultPath);
  if(number != 0)
  {
    _fileName = resultPath;
    _mustBeDeleted = true;
  }
  return number;
}

bool CTempFile::Create(LPCTSTR prefix, CSysString &resultPath)
{
  CSysString tempPath;
  if(!MyGetTempPath(tempPath))
    return false;
  if (Create(tempPath, prefix, resultPath) != 0)
    return true;
  return false;
}

bool CTempFile::Remove()
{
  if (!_mustBeDeleted)
    return true;
  _mustBeDeleted = !DeleteFileAlways(_fileName);
  return !_mustBeDeleted;
}

#ifndef _UNICODE

UINT CTempFileW::Create(LPCWSTR dirPath, LPCWSTR prefix, UString &resultPath)
{
  Remove();
  UINT number = MyGetTempFileName(dirPath, prefix, resultPath);
  if(number != 0)
  {
    _fileName = resultPath;
    _mustBeDeleted = true;
  }
  return number;
}

bool CTempFileW::Create(LPCWSTR prefix, UString &resultPath)
{
  UString tempPath;
  if(!MyGetTempPath(tempPath))
    return false;
  if (Create(tempPath, prefix, resultPath) != 0)
    return true;
  return false;
}

bool CTempFileW::Remove()
{
  if (!_mustBeDeleted)
    return true;
  _mustBeDeleted = !DeleteFileAlways(_fileName);
  return !_mustBeDeleted;
}

#endif

}}}
