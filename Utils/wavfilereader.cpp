#include"wavfilereader.h"
namespace AC {
	WavFileReader::WavFileReader()
	{
	}
	WavFileReader::~WavFileReader()
	{
		CloseFlie();
	}
	bool WavFileReader::OpenWavFile(const std::string& fileName)
	{
		if (_file)
		{
			printf("已经打开了文件!\n");
			return false;
		}
		WaveRIFF riff;
		WaveFormat format;
		WaveData data;
		int userDataSize;
		_file = fopen(fileName.c_str(), "rb+");
		if (!_file)
		{
			printf("打开文件失败!\n");
			return false;
		}
		//读取头部信息
		if (fread(&riff, 1, sizeof(riff), static_cast<FILE*>(_file)) != sizeof(riff))
		{
			printf("文件读取错误，读取riff失败!\n");
			goto error;
		}
		if (std::string(riff.id, 4) != "RIFF" || std::string(riff.waveFlag, 4) != "WAVE")
		{
			printf("头部信息不正确，不是wav文件!\n");
			goto error;
		}
		if (fread(&format, 1, sizeof(format), static_cast<FILE*>(_file)) != sizeof(format))
		{
			printf("文件读取错误，读取format失败!\n");
			goto error;
		}
		if (std::string(format.id, 4) != "fmt ")
		{
			printf("头部信息不正确，缺少fmt!\n");
			goto error;
		}
		if (format.formatTag != 1)
		{
			printf("程序不支持，数据格式非pcm，只支持pcm格式的数据!\n");
			goto error;
		}
		userDataSize = format.blockSize - sizeof(format) + 8;
		if (userDataSize < 0)
		{
			printf("头部信息不正确，blockSize大小异常!\n");
			goto error;
		}
		else if (userDataSize > 0)
		{
			if (fseek(static_cast<FILE*>(_file), userDataSize, SEEK_CUR) != 0)
			{
				printf("文件读取错误!\n");
				goto error;
			}
		}
		while (1)
		{
			if (fread(&data, 1, sizeof(data), static_cast<FILE*>(_file)) != sizeof(data))
			{
				printf("文件读取错误!\n");
				goto error;
			};
			if (std::string(data.id, 4) != "data")
			{
				if (fseek(static_cast<FILE*>(_file), data.dataLength, SEEK_CUR) != 0)
				{
					printf("文件读取错误!\n");
					goto error;
				}
				continue;
			}
			break;
		}
		_dataOffset = ftell(static_cast<FILE*>(_file));
		_fileLength = riff.fileLength+8;
		_dataLength = data.dataLength;
		_channels = format.channels;
		_sampleRate = format.samplesPerSec;
		_bitsPerSample = format.bitsPerSample;
		return true;
	error:
		if (fclose(static_cast<FILE*>(_file)) == EOF)
		{
			printf("文件关闭失败!\n");
		}
		_file = nullptr;
		return false;
	}
	void WavFileReader::CloseFlie()
	{
		if (_file)
		{
			if (fclose(static_cast<FILE*>(_file)) == EOF)
			{
				printf("文件关闭失败!\n");
			}
			_file = nullptr;
		}
	}
	int WavFileReader::ReadData(char* buf, int bufLength)
	{
		if (ftell(static_cast<FILE*>(_file)) >= _dataOffset + _dataLength)
			return 0;
		return fread(buf, 1, bufLength, static_cast<FILE*>(_file));
	}

	void WavFileReader::SetPosition(int postion)
	{
		if (fseek(static_cast<FILE*>(_file), _dataOffset + postion, SEEK_SET) != 0)
		{
			printf("定位失败!\n");
		}
	}
	int WavFileReader::GetPosition()
	{
		return ftell(static_cast<FILE*>(_file)) - _dataOffset;
	}

	int WavFileReader::GetFileLength()
	{
		return _fileLength;
	}

	int WavFileReader::GetDataLength()
	{
		return _dataLength;
	}

	int WavFileReader::GetChannels()
	{
		return _channels;
	}

	int WavFileReader::GetSampleRate()
	{
		return _sampleRate;
	}

	int WavFileReader::GetBitsPerSample()
	{
		return _bitsPerSample;
	}
}
