#include <Windows.h>
#include <stdio.h>
#include <tchar.h>
#include "AsyncReadWrite.h"

struct ReadTestStream : ReadStream
{
	int readCount = 0;
	int read(char* buffer, int bufferSize, int* bytesRead)
	{
		memset(buffer, bufferSize, 0);

		if (readCount == 10)
		{
			*bytesRead = 0;
			return 0;
		}

		// pretend this function takes a while!
		std::this_thread::sleep_for(std::chrono::milliseconds(100));

		char buff[100];
		sprintf_s(buff, "This is read number %d\n", readCount);
		strcpy_s(buffer, sizeof(buff), buff);

		*bytesRead = strlen(buffer);

		readCount++;

		return 0;
	}
};

struct WriteTestStream : WriteStream
{
	int write(char* buffer, int bufferSize, int* bytesWritten)
	{
		// pretend this function takes a while!
		std::this_thread::sleep_for(std::chrono::milliseconds(500));

		printf(buffer);

		*bytesWritten = bufferSize;

		return 0;
	}
};

struct AsyncTestCopyStatus : AsyncCopyStatus
{
	int count = 0;
	void bytesWritten(int bytesWritten) 
	{
		printf("%d bytes written...\n", bytesWritten);

		if (count == 5)
			cancel = true;

		count++;
	};
	void bytesRead(int numberOfBytesRead) 
	{ 
		printf("%d bytes read...\n", numberOfBytesRead);

		
	};
};

std::mutex lock;
std::condition_variable signal;

void Thread1()
{
	while (true)
	{
		std::unique_lock<std::mutex> lk(lock);
		printf("Waiting for signal...\n");
		signal.wait(lk);
		printf("Got signal!\n");
	}
}

void Thread2()
{
	while (true)
	{
		printf("Locking thread 2\n");
		lock.lock();
		printf("Locked thread 2\n");

		Sleep(3000);

		printf("Unlocking thread 2\n");
		lock.unlock();
		printf("Unlocked thread 2\n");
	}
}

int _tmain(int argc, _TCHAR* argv[])
{
	BufferBlockManager bufferBlockManager(5, 4096);
	ReadTestStream readStream;
	WriteTestStream writeStream;
	AsyncTestCopyStatus status;
	int readResult = 0;
	int writeResult = 0;
	bool didFinish = false;

	printf("Starting copy...\n");

	AsyncCopyStream(&bufferBlockManager, &readStream, &writeStream, &readResult, &writeResult, &didFinish, &status);

	printf("Finished copy... readResult=%d writeResult=%d didFinish=%d \n", readResult, writeResult, didFinish);

	getchar();

	return 0;
}