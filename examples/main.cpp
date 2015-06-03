
#include <stdio.h>
#include <tchar.h>
#include "AsyncReadWrite.h"

struct ReadTestStream : ReadStream
{
	int readCount = 0;
	int read(char* buffer, int bufferSize, int* bytesRead)
	{
		printf("Starting read...\n");

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

		printf("Finished read...\n");

		return 0;
	}
};

struct WriteTestStream : WriteStream
{
	int write(char* buffer, int bufferSize, int* bytesWritten)
	{
		printf("Starting write...\n");

		// pretend this function takes a while!
		std::this_thread::sleep_for(std::chrono::milliseconds(500));

		printf(buffer);

		printf("Finished write...\n");

		return 0;
	}
};

int _tmain(int argc, _TCHAR* argv[])
{
	BufferBlockManager bufferBlockManager(5, 4096);
	ReadTestStream readStream;
	WriteTestStream writeStream;
	int readResult = 0;
	int writeResult = 0;

	printf("Starting copy...\n");

	AsyncCopyStream(&bufferBlockManager, &readStream, &writeStream, &readResult, &writeResult);

	printf("Finished copy... readResult=%d writeResult=%d \n", readResult, writeResult);

	getchar();

	return 0;
}
