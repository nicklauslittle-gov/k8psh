// This software is released as part of the k8psh project.
// Portions of this software are public domain or licensed under the MIT license. (See the license file for details.)

#define main k8pshMain
#include "k8psh.cxx"
#undef main

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <thread>

#include "Test.hxx"

static int runCommand(const std::string &command)
{
	std::cout << "Executing \"" << command << '"' << std::endl;

#ifdef _WIN32
	int exitCode = std::system(command.c_str());
#else
	pid_t child = fork();

	if (child == 0)
	{
		(void)execl("/bin/sh", "sh", "-c", command.c_str(), (char *)0);
		LOG_ERROR << "Failed to exec command \"" << command << "\": " << errno;
	}
	else if (child == -1)
		LOG_ERROR << "Failed to fork command \"" << command << "\": " << errno;

	int status = 0;
	pid_t pid = 0;

	while (pid <= 0 || !(WIFEXITED(status) || WIFSIGNALED(status)))
	{
		pid = waitpid(child, &status, 0);

		if (pid < 0 && errno != EINTR)
			LOG_ERROR << "Failed to get exit code of command \"" << command << "\": " << errno;
	}

	if (!WIFEXITED(status))
		LOG_ERROR << "Failed to run command \"" << command << "\": " << status;

	int exitCode = WEXITSTATUS(status);
#endif

	std::cout << "Command \"" << command << "\" returned " << exitCode << std::endl;
	return exitCode;
}

int main(int argc, const char *argv[])
{
	std::string executable = k8psh::Utilities::getExecutablePath();
	std::string basename = k8psh::Utilities::getBasename(argv[0]);
	std::size_t length = basename.length();

	if (length >= 4 && basename.substr(length - 4) == ".exe")
		basename.resize(length - 4);

	// Check for test cases
	if (argc > 1)
	{
		std::string arg = argv[1];

		if (arg == "--name")
			return mainServer(argc, argv);
		else if (arg == "0")
		{
			TEST_THAT(k8psh::Utilities::getEnvironmentVariable("PATH") == ".");
			std::cout << k8psh::Utilities::getEnvironmentVariable("K8PSH_TEST_NAME");
			return 0;
		}
		else if (arg == "1")
		{
			if (argc < 3)
				LOG_ERROR << "Expecting additional argument to command";

			TEST_THAT(k8psh::Utilities::getEnvironmentVariable("PATH") != ".");
			std::cerr << k8psh::Utilities::getEnvironmentVariable("K8PSH_TEST_NAME") << ", " << argv[2];
			return 1;
		}
		else if (arg == "2")
		{
			TEST_THAT(k8psh::Utilities::getEnvironmentVariable("PATH") == ".");
			std::cout << std::cin.rdbuf();

			return 2;
		}
		else if (arg == "3")
		{
			TEST_THAT(k8psh::Utilities::getEnvironmentVariable("PATH") != ".");
			(void)std::fclose(stdin);
			(void)std::fclose(stdout);
			(void)std::fclose(stderr);
			std::this_thread::sleep_for(std::chrono::milliseconds(250));

			return 3;
		}

		return mainClient(argc, argv);
	}
	else if (basename != "k8pshTest")
		return mainClient(argc, argv);

	// Create configuration file
	std::ofstream configFile((basename + ".conf").c_str());

	configFile << "workingDirectory = ." << std::endl;
	configFile << "[k8pshTest]" << std::endl;
	configFile << "'0_" << basename << "' K8PSH_TEST_NAME= PATH= '" << executable << "' 0" << std::endl;
	configFile << "'1_" << basename << "' K8PSH_TEST_NAME= =PATH= '" << executable << "' 1" << std::endl;
	configFile << "'2_" << basename << "' K8PSH_TEST_NAME= ?PATH= '" << executable << "' 2" << std::endl;
	configFile << "'3_" << basename << "' K8PSH_TEST_NAME= ?PATH= '" << executable << "' 3" << std::endl;
	configFile.close();

	// Start server
	k8psh::Utilities::setEnvironmentVariable("K8PSH_CONFIG", basename + ".conf");
	k8psh::Utilities::setEnvironmentVariable("K8PSH_DEBUG", "Main, Process");

	std::thread([executable]{ (void)runCommand(executable + " --name k8pshTest --generate-local-executables --timeout 3000"); }).detach();
	std::this_thread::sleep_for(std::chrono::milliseconds(250));
	k8psh::Utilities::setEnvironmentVariable("K8PSH_DEBUG");

	// Run test cases
	TEST_THAT(k8psh::Utilities::setEnvironmentVariable("PATH", "."));
	TEST_THAT(k8psh::Utilities::setEnvironmentVariable("K8PSH_TEST_NAME", "Test 0"));
	TEST_THAT(runCommand("0_" + basename + " > test.out 2> test.err") == 0);
	TEST_THAT(k8psh::Utilities::readFile("test.out") == static_cast<const std::ostringstream &>(std::ostringstream() << "Test 0").str());
	TEST_THAT(k8psh::Utilities::readFile("test.err").empty());

	TEST_THAT(k8psh::Utilities::setEnvironmentVariable("K8PSH_TEST_NAME", "Test 1"));
	TEST_THAT(runCommand("1_" + basename + " a > test.out 2> test.err") == 1);
	TEST_THAT(k8psh::Utilities::readFile("test.out").empty());
	TEST_THAT(k8psh::Utilities::readFile("test.err") == static_cast<const std::ostringstream &>(std::ostringstream() << "Test 1, a").str());

	TEST_THAT(runCommand("2_" + basename + " < test.err > test.out") == 2);
	TEST_THAT(k8psh::Utilities::readFile("test.out") == static_cast<const std::ostringstream &>(std::ostringstream() << "Test 1, a").str());

	TEST_THAT(k8psh::Utilities::setEnvironmentVariable("PATH", ".;.:."));
	TEST_THAT(runCommand("3_" + basename + " < test.out > test3.out 2> test.err") == 3);
	TEST_THAT(k8psh::Utilities::readFile("test3.out").empty());
	TEST_THAT(k8psh::Utilities::readFile("test.err").empty());
}