# Async Stream-Copy CPP

## What
This is a simple crossplatform c++ implementation of copying one stream to another asynchronously, typically for IO (netowork/file/usb/etc) access. It consits of only a two files (cpp/h).

## Why
There are many great stackoverflow questions (such as [this] (http://stackoverflow.com/questions/1540658/net-asynchronous-stream-read-write) one) that give good examples on how to asyncrously read from one stream, and asyncrounously write to another. However, I couldn't find anyone proposing a solution to the same problem, but for c++.

This is really needed for doing IO access to custom locations. Sure, windows has the ```FILE_FLAG_OVERLAPPED``` support, but what about one the source or destination something the OS knows nothing about? 

## Use case
A good use case would be copying a file to an iOS device (using libimobiledevice). The write operation to iOS over USB slow. Synchronously, this problem gets worse because between each write, you have to read from the file system. If done asynchronously, we can ensure that the write operation has a continous stream of bytes that have been read from another thread. The copy performance improves *greatly* when doing this asynchronously.

## Supported environments

- Windows
- Linux*
- Mac*

\* This implementation has currently only been tested using Windows. Please, feel free to use it on Linux/Mac and let me know how it works. Bonus points if you submit a pull-request!
