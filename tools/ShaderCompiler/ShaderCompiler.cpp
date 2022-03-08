/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#include <common/CRC.h>
#include <common/ShaderBlobEntry.h>
#include "Options.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <map>
#include <list>
#include <stdio.h>
#include <regex>
#include <thread>
#include <mutex>
#include <signal.h>
#include <atomic>
#include <climits>
#if !defined WIN32
#include <unistd.h>
#endif

#if defined WIN32
#define UNLINK _unlink
#define POPEN _popen
#define PCLOSE _pclose
#define PUTENV _putenv
#else
#define UNLINK unlink
#define POPEN popen
#define PCLOSE pclose
#define PUTENV putenv
#define SETENV setenv
#endif

namespace fs = std::filesystem;
namespace sdk = KickstartRT;

CommandLineOptions g_Options;
std::string g_PlatformName;

struct CompileTask
{
	std::string sourceFile;
	std::string shaderName;
	std::string entryPoint;
	std::string combinedDefines;
	std::string commandLine;
};

std::vector<CompileTask> g_CompileTasks;
int g_OriginalTaskCount;
std::atomic<int> g_ProcessedTaskCount;
std::mutex g_TaskMutex;
std::mutex g_ReportMutex;
bool g_Terminate = false;
bool g_CompileSuccess = true;

struct BlobEntry
{
	fs::path compiledPermutationFile;
	uint32_t defineHash;
	std::string hashKey;
};

std::map<std::string, std::vector<BlobEntry>> g_ShaderBlobs;

std::map<fs::path, fs::file_time_type> g_HierarchicalUpdateTimes;
const std::vector<fs::path> g_IgnoreIncludes = { "util/util.h" };

#if 0
const char* g_DxcOptions      = "-O3 -nologo -Zpr -WX -Wno-ignored-attributes -D GRAPHICS_API_D3D ";
#else
const char* g_DxcOptions = "-O3 -nologo -Zpr -WX -D GRAPHICS_API_D3D ";
#endif

const char* g_DxcSpirvOptions = "-O3 -nologo -Zpr -WX -spirv -fspv-target-env=vulkan1.2 -D GRAPHICS_API_VK ";

const char* g_IgnoreDescriptorSets = 
	"\"-DDESCRIPTOR_SET_0= \" "
	"\"-DDESCRIPTOR_SET_1= \" "
	"\"-DDESCRIPTOR_SET_2= \" "
	"\"-DDESCRIPTOR_SET_3= \" "
	"\"-DDESCRIPTOR_SET_4= \" "
	"\"-DDESCRIPTOR_SET_5= \" "
	"\"-DDESCRIPTOR_SET_6= \" "
	"\"-DDESCRIPTOR_SET_7= \" "
	"\"-DDESCRIPTOR_SET_8= \" "
	"\"-DDESCRIPTOR_SET_9= \" ";

const char* g_VulkanDescriptorSets = 
	"-DDESCRIPTOR_SET_0=,space0 "
	"-DDESCRIPTOR_SET_1=,space1 "
	"-DDESCRIPTOR_SET_2=,space2 "
	"-DDESCRIPTOR_SET_3=,space3 "
	"-DDESCRIPTOR_SET_4=,space4 "
	"-DDESCRIPTOR_SET_5=,space5 "
	"-DDESCRIPTOR_SET_6=,space6 "
	"-DDESCRIPTOR_SET_7=,space7 "
	"-DDESCRIPTOR_SET_8=,space8 "
	"-DDESCRIPTOR_SET_9=,space9 ";

int getBindingOffset(const std::string& target, int resourceType)
{
	int stageBase = 0;
	switch (target[0])
	{
	case 'v': stageBase = 0; break;
	case 'h': stageBase = 1; break;
	case 'd': stageBase = 2; break;
	case 'g': stageBase = 3; break;
	case 'p': stageBase = 4; break;
	case 'c': stageBase = 0; break;
	}

	return stageBase * g_Options.vulkanBindingsPerStage + resourceType * g_Options.vulkanBindingsPerResourceType;
}

std::string path_string(fs::path path)
{
	return path.make_preferred().string();
}

bool getHierarchicalUpdateTime(const fs::path& rootFilePath, std::list<fs::path>& callStack, fs::file_time_type& outTime)
{
	static std::basic_regex<char> include_pattern("\\s*#include\\s+[\"<]([^>\"]+)[>\"].*");

	auto found = g_HierarchicalUpdateTimes.find(rootFilePath);
	if (found != g_HierarchicalUpdateTimes.end())
	{
		outTime = found->second;
		return true;
	}

	std::ifstream inputFile(rootFilePath);
	if (!inputFile.is_open())
	{
		std::cout << "ERROR: Cannot open file  " << path_string(rootFilePath) << std::endl;
		for (const fs::path& otherPath : callStack)
			std::cout << "            included in  " << path_string(otherPath) << std::endl;

		return false;
	}

	callStack.push_front(rootFilePath);

	fs::path rootBasePath = rootFilePath.parent_path();
	fs::file_time_type hierarchicalUpdateTime = fs::last_write_time(rootFilePath);

	uint32_t lineno = 0;
	for (std::string line; std::getline(inputFile, line);)
	{
		lineno++;

		std::match_results<const char*> result;
		std::regex_match(line.c_str(), result, include_pattern);
		if (!result.empty())
		{
			fs::path include = std::string(result[1]);

			bool ignore = false;
			for (const fs::path& ignoredPath : g_IgnoreIncludes)
			{
				if (ignoredPath == include)
				{
					ignore = true;
					break;
				}
			}

			if (ignore)
				continue;

			bool found = false;
			fs::path includedFilePath = rootBasePath / include;
			if (fs::exists(includedFilePath))
			{
				found = true;
			}
			else
			{
				for (const std::string& includePath : g_Options.includePaths)
				{
					includedFilePath = includePath / include;
					if (fs::exists(includedFilePath))
					{
						found = true;
						break;
					}
				}
			}

			if (!found)
			{
				std::cout << "ERROR: Cannot find include file  " << path_string(include) << std::endl;
				for (const fs::path& otherPath : callStack)
					std::cout << "                    included in  " << path_string(otherPath) << std::endl;

				return false;
			}

			fs::file_time_type dependencyTime;
			if (!getHierarchicalUpdateTime(includedFilePath, callStack, dependencyTime))
				return false;

			hierarchicalUpdateTime = std::max(dependencyTime, hierarchicalUpdateTime);
		}
	}

	callStack.pop_front();

	g_HierarchicalUpdateTimes[rootFilePath] = hierarchicalUpdateTime;
	outTime = hierarchicalUpdateTime;

	return true;
}

uint32_t GetInputsHash(const CommandLineOptions& options) {
	sdk::CRC::CrcHash hasher;
	for (const std::string& define : options.definitions)
		hasher.AddBytes(define.c_str(), define.length());
	for (const std::string& dir : options.includePaths)
		hasher.AddBytes(dir.c_str(), dir.length());
	return hasher.Get();
}

std::string buildCompilerCommandLine(const CompilerOptions& options, const fs::path& shaderFile, const fs::path& outputFile)
{
	std::ostringstream ss;

#ifdef WIN32
	ss << "%COMPILER% ";
#else
	ss << "$COMPILER ";
#endif

	ss << path_string(shaderFile) << " ";
	ss << "-Fo " << path_string(outputFile) << " ";
	ss << "-T " << options.target << " ";
	if (!options.entryPoint.empty())
		ss << "-E " << options.entryPoint << " ";
	for (const std::string& define : options.definitions)
		ss << "-D" << define << " ";
	for (const std::string& define : g_Options.definitions)
		ss << "-D" << define << " ";
	for (const std::string& dir : g_Options.includePaths)
		ss << "-I" << path_string(dir) << " ";

	switch (g_Options.platform)
	{
	case Platform::DXIL:
		ss << g_DxcOptions;
		ss << g_IgnoreDescriptorSets;
		break;
	case Platform::SPIRV:
		ss << g_DxcSpirvOptions;
		ss << g_VulkanDescriptorSets;

		for (int space = 0; space < 10; space++)
		{
			ss << "-fvk-t-shift " << getBindingOffset(options.target, 0) << " " << space << " ";
			ss << "-fvk-s-shift " << getBindingOffset(options.target, 1) << " " << space << " ";
			ss << "-fvk-b-shift " << getBindingOffset(options.target, 2) << " " << space << " ";
			ss << "-fvk-u-shift " << getBindingOffset(options.target, 3) << " " << space << " ";
		}
		break;
	}

	for (const std::string& option : g_Options.additionalCompilerOptions)
	{
		ss << option << " ";
	}

	return ss.str();
}

void printError(uint32_t lineno, const std::string& error)
{
	std::cerr << g_Options.inputFile << "(" << lineno << "): " << error << std::endl;
}

bool processShaderConfig(uint32_t lineno, const std::string& shaderConfig)
{
	CompilerOptions compilerOptions;
	if (!compilerOptions.parse(shaderConfig))
	{
		printError(lineno, compilerOptions.errorMessage);
		return false;
	}

	sdk::CRC::CrcHash hasher;
	std::ostringstream hashKey;
	std::ostringstream combinedDefines;
	for (const std::string& define : compilerOptions.definitions)
	{
		const char semicolon = ';';
		hasher.AddBytes(define.c_str(), define.size());
		hasher.AddBytes(&semicolon, 1);

		hashKey << define << ";";
		combinedDefines << define << " ";
	}
	uint32_t defineHash = hasher.Get();

	fs::path compiledShaderName = compilerOptions.shaderName;
	compiledShaderName.replace_extension("");
	if (!compilerOptions.entryPoint.empty() && compilerOptions.entryPoint != "main")
		compiledShaderName += "_" + compilerOptions.entryPoint;

	fs::path sourceFile = fs::path(g_Options.inputFile).parent_path() / compilerOptions.shaderName;

	fs::path compiledShaderPath = g_Options.outputPath / compiledShaderName.parent_path();
	if (!fs::exists(compiledShaderPath))
	{
		std::cout << "INFO: Creating directory " << compiledShaderPath << std::endl;
		fs::create_directories(compiledShaderPath);
	}
	else if(!g_Options.force)
	{
		// Generate hash from inputs.
		fs::path compiledShaderFile = g_Options.outputPath / compiledShaderName;
		compiledShaderFile += ".bin";
		if (fs::exists(compiledShaderFile))
		{
			uint32_t fileInputsHash;
			std::ifstream fstr;
			fstr.open(compiledShaderFile, std::ios::binary);
			fstr.seekg(sizeof(uint32_t)); // Signature
			fstr.read((char*)&fileInputsHash, sizeof(uint32_t)); // Hash
			fstr.close();

			uint32_t inputsHash = GetInputsHash(g_Options);

			if (fileInputsHash == inputsHash) {
				// Check if dependents have been updated...
				fs::file_time_type compiledFileTime = fs::last_write_time(compiledShaderFile);

				fs::file_time_type sourceHierarchyTime;
				std::list<fs::path> callStack;
				if (!getHierarchicalUpdateTime(sourceFile, callStack, sourceHierarchyTime))
					return false;

				if (compiledFileTime > sourceHierarchyTime)
					return true;
			}
		}
	}

	fs::path compiledPermutationName = compiledShaderName;

	{
		char buf[16];
		sprintf(buf, "_%08x", defineHash);
		compiledPermutationName += buf;
	}
	compiledPermutationName += ".bin";

	fs::path compiledPermutationFile = g_Options.outputPath / compiledPermutationName;

	std::string commandLine = buildCompilerCommandLine(compilerOptions, sourceFile, compiledPermutationFile);
	
	CompileTask task;
	task.sourceFile = sourceFile.generic_string();
	task.shaderName = compilerOptions.shaderName;
	task.entryPoint = compilerOptions.entryPoint;
	task.combinedDefines = combinedDefines.str();
	task.commandLine = commandLine;
	g_CompileTasks.push_back(task);

	{
		BlobEntry entry;
		entry.compiledPermutationFile = compiledPermutationFile;
		entry.defineHash = defineHash;
		entry.hashKey = hashKey.str();

		std::vector<BlobEntry>& entries = g_ShaderBlobs[path_string(compiledShaderName)];
		entries.push_back(entry);
	}

	return true;
}

bool expandPermutations(uint32_t lineno, const std::string& shaderConfig)
{
	size_t opening = shaderConfig.find('{');
	if (opening != std::string::npos)
	{
		size_t closing = shaderConfig.find('}', opening);
		if (closing == std::string::npos)
		{
			printError(lineno, "missing }");
			return false;
		}

		size_t current = opening + 1;
		while(true)
		{
			size_t comma = shaderConfig.find(',', current);

			if (comma == std::string::npos || comma > closing)
				comma = closing;

			std::string newConfig = shaderConfig.substr(0, opening)
				+ shaderConfig.substr(current, comma - current) 
				+ shaderConfig.substr(closing + 1);

			if (!expandPermutations(lineno, newConfig))
				return false;

			current = comma + 1;

			if(comma >= closing)
				break;
		}

		return true;
	}

	return processShaderConfig(lineno, shaderConfig);
}

bool trim(std::string& s)
{
	size_t pos;
	pos = s.find('#');
	if (pos != std::string::npos)
		s.erase(pos, s.size() - pos);

	pos = s.find_first_not_of(" \t");
	if (pos == std::string::npos)
		return false;
	else
		s.erase(0, pos);

	return true;
}

bool WriteShaderBlob(const std::string& compiledShaderName, const std::vector<BlobEntry>& entries)
{
	fs::path outputFilePath = fs::path(g_Options.outputPath) / (compiledShaderName + ".bin");
	std::string outputFileName = path_string(outputFilePath);

	FILE* outputFile = fopen(outputFileName.c_str(), "wb");
	if (!outputFile)
	{
		std::cout << "ERROR: cannot write " << outputFileName << std::endl;
		return false;
	}

	if (g_Options.verbose)
	{
		std::cout << "INFO: writing " << outputFileName << std::endl;
	}

	fwrite(KickstartRT::ShaderBlob::kBlobSignature, 1, KickstartRT::ShaderBlob::kBlobSignatureSize, outputFile);
	uint32_t inputsHash = GetInputsHash(g_Options);
	fwrite(&inputsHash, 1, 4, outputFile);

	for (const BlobEntry& entry : entries)
	{
		std::string inputFileName = path_string(entry.compiledPermutationFile);
		FILE* inputFile = fopen(inputFileName.c_str(), "rb");

		if (!inputFile)
		{
			std::cout << "ERROR: cannot read " << inputFileName << std::endl;
			fclose(outputFile);
			return false;
		}

		fseek(inputFile, 0, SEEK_END);
		size_t fileSize = ftell(inputFile);
		fseek(inputFile, 0, SEEK_SET);

		if (fileSize == 0)
		{
			fclose(inputFile);
			continue;
		}

		if (fileSize > UINT_MAX)
		{
			std::cout << "ERROR: binary shader file too big: " << inputFileName << std::endl;
			fclose(inputFile);
			continue;
		}

		void* buffer = malloc(fileSize);
		fread(buffer, 1, fileSize, inputFile);
		fclose(inputFile);

		if (!g_Options.keep)
		{
			UNLINK(inputFileName.c_str());
		}

		sdk::CRC::CrcHash hasher;
		hasher.AddBytes((char*)buffer, fileSize);
		sdk::ShaderBlob::ShaderBlobEntry binaryEntry;
		binaryEntry.hashKeySize = (uint32_t)entry.hashKey.size();
		binaryEntry.dataSize = (uint32_t)fileSize;
		binaryEntry.dataCrc = hasher.Get();
		binaryEntry.defineHash = entry.defineHash;

		fwrite(&binaryEntry, 1, sizeof(binaryEntry), outputFile);
		fwrite(entry.hashKey.data(), 1, entry.hashKey.size(), outputFile);
		fwrite(buffer, 1, fileSize, outputFile);
	}

	fclose(outputFile);

	return true;
}

void compileThreadProc()
{
	while (!g_Terminate)
	{
		CompileTask task;
		{
			std::lock_guard<std::mutex> guard(g_TaskMutex);
			if (g_CompileTasks.empty())
				return;

			task = g_CompileTasks[g_CompileTasks.size() - 1];
			g_CompileTasks.pop_back();
		}

		std::string commandLine = task.commandLine + " 2>&1";

		FILE* pipe = POPEN(commandLine.c_str(), "r");
		if (!pipe)
		{
			std::lock_guard<std::mutex> guard(g_ReportMutex);
			std::cout << "ERROR: cannot run " << g_Options.compilerPath << std::endl;
			g_CompileSuccess = false;
			g_Terminate = true;
			return;
		}

		std::ostringstream ss;
		char buf[1024];
		while (fgets(buf, sizeof(buf), pipe))
			ss << buf;

		int result = PCLOSE(pipe);
		g_ProcessedTaskCount++;

		{
			std::lock_guard<std::mutex> guard(g_ReportMutex);

			const char* resultCode = (result == 0) ? " OK  " : "FAIL ";
			float progress = (float)g_ProcessedTaskCount / (float)g_OriginalTaskCount;

			sprintf(buf, "[%5.1f%%] %s %s %s:%s %s", 
				progress * 100.f, 
				g_PlatformName.c_str(), 
				resultCode, 
				task.shaderName.c_str(), 
				task.entryPoint.c_str(), 
				task.combinedDefines.c_str());

			std::cout << buf << std::endl;
 
			if (result != 0 && !g_Terminate)
			{
				std::cout << "ERRORS for " << task.shaderName << ":" << task.entryPoint << " " << task.combinedDefines << ": " << std::endl;
				std::cout << ss.str() << std::endl;
				g_CompileSuccess = false;
			}
		}
	}
}

void signal_handler(int sig)
{
	g_Terminate = true;

	std::lock_guard<std::mutex> guard(g_ReportMutex);
	std::cout << "SIGINT received, terminating" << std::endl;
}

std::vector<std::string> splitStr(const std::string& s, char delim)
{
	std::vector<std::string> elems;
	std::string item;
	for (char ch : s) {
		if (ch == delim) {
			if (!item.empty())
				elems.push_back(item);
			item.clear();
		}
		else {
			item += ch;
		}
	}
	if (!item.empty())
		elems.push_back(item);
	return elems;
}

int main(int argc, char** argv)
{
	if (!g_Options.parse(argc, argv))
	{
		std::cout << g_Options.errorMessage << std::endl;
		return 1;
	}

	switch (g_Options.platform)
	{
	case Platform::DXIL: g_PlatformName = "DXIL"; break;
	case Platform::SPIRV: g_PlatformName = "SPIR-V"; break;
	}

	// check -Fd option which may be a destination folder of PDB files. If it doen't exist, create it.
	{
		bool afterFd = false;
		std::string afterFdStr;
		for (auto&& o : g_Options.additionalCompilerOptions) {
			std::vector tokenized = splitStr(o, ' ');
			for (auto&& t : tokenized) {
				if (t == "/Fd" || t == "-Fd") {
					afterFd = true;
					continue;
				}
				if (afterFd) {
					afterFdStr = t;
					break;
				}
			}
			if (!afterFdStr.empty())
				break;
		}

		if (afterFdStr.length() > 1) {
			std::string endS = afterFdStr.substr(afterFdStr.length() - 1);
			if (endS == "/" || endS == "\\") {
				// it's a folder so make sure it exists.
				if (!fs::exists(afterFdStr))
				{
					std::cout << "INFO: Creating directory " << afterFdStr << std::endl;
					fs::create_directories(afterFdStr);
				}
			}
		}
	}
	
	std::ifstream configFile(g_Options.inputFile);
	uint32_t lineno = 0;
	for(std::string line; getline(configFile, line);)
	{
		lineno++;

		if (!trim(line))
			continue;

		if (!expandPermutations(lineno, line))
			return 1;
	}

	if (g_CompileTasks.empty())
	{
		std::cout << "All " << g_PlatformName << " outputs are up to date." << std::endl;
		return 0;
	}

	// writing a resouce file to link shaders with a DLL or EXE on windows.
	if (! g_Options.resourceFilePath.empty()) {
		fs::path rcFilePath(g_Options.resourceFilePath);
		if (!rcFilePath.is_absolute()) {
			rcFilePath = fs::path(g_Options.outputPath) / rcFilePath;
		}
		rcFilePath.make_preferred();
		std::string rcFileName = rcFilePath.string();

		configFile.clear();
		configFile.seekg(0);

		std::ofstream rcFile;
		rcFile.open(rcFileName, std::ios::binary);
		if (!rcFile.is_open()) {
			std::cout << "Failed to open  " <<rcFileName << " to wrrite." << std::endl;
			return 1;
		}
		for (std::string line; getline(configFile, line);) {
			CompilerOptions compilerOptions;
			compilerOptions.parse(line);

			fs::path binaryName(compilerOptions.shaderName);
			binaryName.replace_extension(".bin");

			fs::path binaryPath(g_Options.outputPath);
			binaryPath /= binaryName;
			binaryPath.make_preferred();
			std::string binaryPathStr = binaryPath.string();
			binaryPathStr = std::regex_replace(binaryPathStr, std::regex("\\\\"), "\\\\");

			rcFile << binaryName.string() << " BINARY " << "\"" << binaryPathStr << "\"" << "\r\n";
		}
		rcFile.close();
		std::cout << "Resource file \"" << rcFileName << "\" has been updated." << std::endl;
	}

	g_OriginalTaskCount = (int)g_CompileTasks.size();
	g_ProcessedTaskCount = 0;

	{
		// Workaround for weird behavior of _popen / cmd.exe on Windows
		// with quotes around the executable name and also around some other arguments.
		std::ostringstream ss;
		ss << "COMPILER=\"" << g_Options.compilerPath << "\"";

#if WIN32
		PUTENV((char *)ss.str().c_str());
#else
		// Linux's version of putenv does not take a copy of the string, so 
		// it is destroyed when ss is out of scope. Hence, use setenv() instead.
		SETENV("COMPILER", g_Options.compilerPath.c_str(), 1);
#endif
	}

	unsigned int threadCount = std::thread::hardware_concurrency();
	if (threadCount == 0 || !g_Options.parallel)
	{
		threadCount = 1;
	}

	signal(SIGINT, signal_handler);

	std::vector<std::thread> threads;
	threads.resize(threadCount);
	for (unsigned int threadIndex = 0; threadIndex < threadCount; threadIndex++)
	{
		threads[threadIndex] = std::thread(compileThreadProc);
	}
	for (unsigned int threadIndex = 0; threadIndex < threadCount; threadIndex++)
	{
		threads[threadIndex].join();
	}

	if (!g_CompileSuccess || g_Terminate)
		return 1;

	for (const std::pair<const std::string, std::vector<BlobEntry>>& it : g_ShaderBlobs)
	{
		if (!WriteShaderBlob(it.first, it.second))
			return 1;
	}

	return 0;
}
