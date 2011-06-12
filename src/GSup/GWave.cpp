/*
	Copyright (C) 2006, Mike Gashler

	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Lesser General Public
	License as published by the Free Software Foundation; either
	version 2.1 of the License, or (at your option) any later version.

	see http://www.gnu.org/copyleft/lesser.html
*/

#include "GWave.h"
#include "../GClasses/GError.h"
#include <fstream>
#include "math.h"

#ifdef WINDOWS
#	pragma pack(1)
#endif

namespace GClasses {

struct WaveHeader
{
	// RIFF header
	unsigned char RIFF[4]; // "RIFF"
	unsigned int dwSize; // Size of data to follow
	unsigned char WAVE[4]; // "WAVE"

	// FMT sub-chunk
	unsigned char fmt_[4]; // "fmt "
	unsigned int dw16; // 16 (the size of the rest of the fmt sub-chunk)
	unsigned short wOne_0; // 1 (means PCM with no compression)
	unsigned short wChnls; // Number of Channels
	unsigned int dwSRate; // Sample Rate
	unsigned int BytesPerSec; // SampleRate * NumChannels * BitsPerSample / 8
	unsigned short wBlkAlign; // NumChannels * BitsPerSample/8
	unsigned short BitsPerSample; // Sample size

	// DATA sub-chunk
	unsigned char DATA[4]; // "DATA"
	unsigned int dwDSize; // Number of bytes of data
};

#ifdef WIN32
#	pragma pack()
#endif


GWave::GWave()
: m_sampleCount(0),
m_channels(0),
m_sampleRate(0),
m_bitsPerSample(0),
m_pData(NULL)
{
}

GWave::~GWave()
{
	delete[] m_pData;
}

void GWave::setData(unsigned char* pData, int bitsPerSample, int sampleCount, int channels, int sampleRate)
{
	delete[] m_pData;
	m_pData = pData;
	m_sampleCount = sampleCount;
	m_bitsPerSample = bitsPerSample;
	m_channels = channels;
	m_sampleRate = sampleRate;
}

void GWave::load(const char* szFilename)
{
	// Read in the wave header
	std::ifstream s;
	s.exceptions(std::ios::failbit|std::ios::badbit);
	try
	{
		s.open(szFilename, std::ios::binary);
	}
	catch(const std::exception&)
	{
		ThrowError("Failed to open file: ", szFilename);
	}
	struct WaveHeader waveHeader;
	s.read((char*)&waveHeader, sizeof(struct WaveHeader));

	// Get stats from the wave header
	int size = waveHeader.dwDSize;
	m_channels = waveHeader.wChnls;
	m_sampleRate = waveHeader.dwSRate;
	m_bitsPerSample = waveHeader.BitsPerSample;
	m_sampleCount = size / (m_bitsPerSample / 8);

	// Read the wave data
	m_pData = new unsigned char[size];
	s.read((char*)m_pData, size);
}

void GWave::save(const char* szFilename)
{
	// Write the wave header
	int size = m_sampleCount * m_channels * m_bitsPerSample / 8;
	struct WaveHeader waveHeader;
	waveHeader.RIFF[0] = 'R';
	waveHeader.RIFF[1] = 'I';
	waveHeader.RIFF[2] = 'F';
	waveHeader.RIFF[3] = 'F';
	waveHeader.dwSize = sizeof(WaveHeader) - 8 + size;
	waveHeader.WAVE[0] = 'W';
	waveHeader.WAVE[1] = 'A';
	waveHeader.WAVE[2] = 'V';
	waveHeader.WAVE[3] = 'E';
	waveHeader.fmt_[0] = 'f';
	waveHeader.fmt_[1] = 'm';
	waveHeader.fmt_[2] = 't';
	waveHeader.fmt_[3] = ' ';
	waveHeader.dw16 = 16;
	waveHeader.wOne_0 = 1;
	waveHeader.wChnls = m_channels;
	waveHeader.dwSRate = m_sampleRate;
	waveHeader.BytesPerSec = m_sampleRate * m_channels * m_bitsPerSample / 8;
	waveHeader.wBlkAlign = m_channels * m_bitsPerSample / 8;
	waveHeader.BitsPerSample = m_bitsPerSample;
	waveHeader.DATA[0] = 'd';
	waveHeader.DATA[1] = 'a';
	waveHeader.DATA[2] = 't';
	waveHeader.DATA[3] = 'a';
	waveHeader.dwDSize = size;

	// Make the file
	std::ofstream os;
	os.exceptions(std::ios::failbit|std::ios::badbit);
	try
	{
		os.open(szFilename, std::ios::binary);
	}
	catch(const std::exception&)
	{
		ThrowError("Error creating file: ", szFilename);
	}
	os.write((const char*)&waveHeader, sizeof(struct WaveHeader));
	os.write((const char*)m_pData, size);
}




GWaveIterator::GWaveIterator(GWave& wave)
: m_wave(wave)
{
	m_pPos = wave.data();
	m_remaining = wave.sampleCount();
	m_pSamples = new double[wave.channels()];
}

GWaveIterator::~GWaveIterator()
{
	delete[] m_pSamples;
}

size_t GWaveIterator::remaining()
{
	return m_remaining;
}

bool GWaveIterator::advance()
{
	if(m_remaining == 0)
		return false;
	m_pPos += m_wave.channels() * m_wave.bitsPerSample() / 8;
	m_remaining--;
	return true;
}

double* GWaveIterator::current()
{
	unsigned char* pX = m_pPos;
	double* pY = m_pSamples;
	for(unsigned short i = 0; i < m_wave.channels(); i++)
	{
		switch(m_wave.bitsPerSample())
		{
			case 8: *pY = (double(*pX) - 0x80) / 0x80; break; // 8-bit is unsigned
			case 16: *pY = double(*(short*)pX) / 0x8000; break; // 16-bit is signed
			case 32: *pY = double(*(int*)pX) / 0x80000000; break; // 32-bit is signed
		}
		pX += m_wave.bitsPerSample() / 8;
		pY++;
	}
	return m_pSamples;
}

void GWaveIterator::set(double* pSamples)
{
	unsigned char* pX = m_pPos;
	double* pY = pSamples;
	for(unsigned short i = 0; i < m_wave.channels(); i++)
	{
		switch(m_wave.bitsPerSample())
		{
			case 8: *pX = (unsigned char)std::max(0.0, std::min(255.0, floor((*pY + 1) * 0x80 + 0.5))); break;
			case 16: *(short*)pX = (short)std::max(-32768.0, std::min(32767.0, floor(*pY * 0x8000 + 0.5))); break;
			case 32: *(int*)pX = (int)std::max(-2147483648.0, std::min(2147483647.0, floor(*pY * 0x80000000 + 0.5))); break;
		}
		pX += m_wave.bitsPerSample() / 8;
		pY++;
	}
}

} // namespace GClasses

