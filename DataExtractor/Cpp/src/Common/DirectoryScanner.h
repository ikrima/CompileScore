#pragma once

namespace IO
{ 
	class DirectoryScanner
	{ 
	public:
		DirectoryScanner(const char* pathToScan, const char* extension);
		~DirectoryScanner();

		const char* SeekNext();
	private: 
		 struct Impl; 
		 Impl* m_impl;
	};
}
