#pragma once
#include<string>
/************************************************************************
* @Project:  	AC.WavFileWriter
* @Decription:  wav文件读取工具
* 本版本只支持pcm读取，且未处理字节顺序。	riff文件是小端，通常在intel的设备上是没问题的，在java虚拟机上则需要处理。
* @Verision:  	v1.0.0.0
* @Author:  	Xin Nie
* @Create:  	2019/4/10 11:10:17
* @LastUpdate:  2019/4/16 10:45:00
************************************************************************
* Copyright @ 2019. All rights reserved.
************************************************************************/
#pragma pack(push,1)
	struct WaveRIFF {
		const	char id[4] = { 'R','I', 'F', 'F' };
		uint32_t fileLength;
		const	char waveFlag[4] = { 'W','A', 'V', 'E' };
	};
	struct WaveFormat
	{
		const	char id[4] = { 'f','m', 't', ' ' };
		uint32_t blockSize = 16;
		uint16_t formatTag;
		uint16_t channels;
		uint32_t samplesPerSec;
		uint32_t avgBytesPerSec;
		uint16_t blockAlign;
		uint16_t  bitsPerSample;
	};
	struct  WaveData
	{
		const	char id[4] = { 'd','a', 't', 'a' };
		uint32_t dataLength;
	};
#pragma pack(pop)

namespace AC {
	/// <summary>
	/// wav文件读取对象
	/// </summary>
	class WavFileReader {
	public:
		/// <summary>
		/// 构造方法
		/// </summary>
		WavFileReader();
		/// <summary>
	    /// 析构方法
	    /// </summary>
		~WavFileReader();
		/// <summary>
		/// 打开wav文件
		/// </summary>
		/// <param name="fileName">文件名</param>
		/// <returns>是否打开成功</returns>
		bool OpenWavFile(const std::string& fileName);
		/// <summary>
		/// 关闭文件
		/// </summary>
		void CloseFlie();
		/// <summary>
		/// 读取音频数据
		/// </summary>
		/// <param name="buf">外部缓存</param>
		/// <param name="bufLength">缓存长度</param>
		/// <returns>读取长度</returns>
		int ReadData(char* buf, int bufLength);
		/// <summary>
		/// 设置读取位置
		/// </summary>
		/// <param name="position"> 读取位置</param>
		void SetPosition(int position);
		/// <summary>
		/// 获取读取位置
		/// </summary>
		/// <returns>读取位置</returns>
		int GetPosition();
		/// <summary>
		/// 获取文件长度
		/// </summary>
		/// <returns>文件长度</returns>
		int GetFileLength();
		/// <summary>
		/// 获取音频数据长度
		/// </summary>
		/// <returns>音频数据长度</returns>
		int GetDataLength();
		/// <summary>
		/// 获取声道数
		/// </summary>
		/// <returns>声道数</returns>
		int GetChannels();
		/// <summary>
		/// 获取采样率
		/// </summary>
		/// <returns>采样率，单位：hz</returns>
		int GetSampleRate();
		/// <summary>
		/// 获取位深
		/// </summary>
		/// <returns>位深，单位：bits</returns>
		int GetBitsPerSample();
	private:
		void* _file = nullptr;
		uint32_t _fileLength = 0;
		uint32_t _dataLength = 0;
		int _channels = 0;
		int  _sampleRate = 0;
		int  _bitsPerSample = 0;
		int _dataOffset = 0;
	};
}
