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
/*
License for cxxopts

Copyright (c) 2014 Jarryd Beck

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
#include <cxxopts.hpp>
#include <filesystem>

#include "Options.h"

using namespace std;
using namespace cxxopts;

bool CommandLineOptions::parse(int argc, char** argv)
{
	Options options("shaderCompiler", "Batch shader compiler for KickStartRTX");

	string platformName;

	options.add_options()
		("i,infile", "File with the list of shaders to compiler", value(inputFile))
		("o,out", "Output directory", value(outputPath))
		("p,parallel", "Compile shaders in multiple CPU threads", value(parallel))
		("v,verbose", "Print commands before executing them", value(verbose))
		("f,force", "Treat all source files as modified", value(force))
		("k,keep", "Keep intermediate files", value(keep))
		("r,res", "Write Resource file", value(resourceFilePath))
		("c,compiler", "Path to the compiler executable (FXC or DXC)", value(compilerPath))
		("I,include", "Include paths", value(includePaths))
		("D,definition", "Definitions", value(definitions))
		("cflags", "Additional compiler command line options", value(additionalCompilerOptions))
		("P,platform", "Target shader bytecode type, one of: DXBC, DXIL, SPIRV", value(platformName))
		("h,help", "Print the help message", value(help));

	try
	{
		options.parse(argc, argv);

		if (help)
		{
			errorMessage = options.help();
			return false;
		}

		if (compilerPath.empty())
			throw OptionException("Compiler path not specified");

		if(!filesystem::exists(compilerPath))
			throw OptionException("Specified compiler executable (" + compilerPath + ") does not exist");

		if (inputFile.empty())
			throw OptionException("Input file not specified");

		if (!filesystem::exists(inputFile))
			throw OptionException("Specified input file (" + inputFile + ") does not exist");

		if (outputPath.empty())
			throw OptionException("Output path not specified");

		if(platformName.empty())
			throw OptionException("Platform not specified");

		for (char& c : platformName)
			c = toupper(c);
		if (platformName == "DXIL")
			platform = Platform::DXIL;
		else if (platformName == "SPIRV" || platformName == "SPIR-V")
			platform = Platform::SPIRV;
		else
			throw OptionException("Unrecognized platform: " + platformName);

		return true;
	}
	catch (const OptionException& e)
	{
		errorMessage = e.what();
		return false;
	}
}

bool CompilerOptions::parse(std::string line)
{
	std::vector<char*> tokens;

	const char* delimiters = " \t";
	char* name = strtok(const_cast<char*>(line.c_str()), delimiters);
	tokens.push_back(name); // argv[0]

	shaderName = name;

	while (char* token = strtok(NULL, delimiters))
		tokens.push_back(token);

	Options options("shaderCompilerConfig", "Configuration options for a shader");

	options.add_options()
		("E", "Entry point", value(entryPoint))
		("T", "Shader target", value(target))
		("D", "Definitions", value(definitions));

	try
	{
		int argc = (int)tokens.size();
		char** argv = tokens.data();
		options.parse(argc, argv);

		if (target.empty())
			throw OptionException("Shader target not specified");

	}
	catch (const OptionException& e)
	{
		errorMessage = e.what();
		return false;
	}

	return true;
}
