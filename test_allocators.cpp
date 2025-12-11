#include "src/memory/SmallAllocator.h"
#include "src/memory/LargeMemoryAllocator.h"
#include "src/core/Vector.h"
#include <iostream>
#include <cassert>

void testSmallAllocatorFragmentation()
{
	std::cout << "\n=== Test: SmallAllocator Fragmentation (Reproduce SceneManager crash) ===" << std::endl;

	SmallAllocator allocator;
	Vector<float> persistentVec(allocator);

	std::cout << "Simulating SceneManager pattern: persistent vector with clear/reserve cycles..." << std::endl;
	std::cout << "This mimics m_tempDebugLineData which persists across frames" << std::endl;

	// Simulate varying frame loads like in a real scene
	// Using larger sizes to stress the 65KB SmallAllocator pool
	// Each float is 4 bytes, so 16000 floats = 64KB (near pool limit)
	size_t frameSizes[] = {1000, 5000, 2000, 8000, 1500, 10000, 3000, 12000, 2500, 15000,
	                       4000, 16000, 3500, 14000, 4500, 13000, 5000, 12000, 6000, 16000};

	for(int frame = 0; frame < 20; ++frame)
	{
		size_t targetSize = frameSizes[frame];
		std::cout << "\nFrame " << frame << ": processing " << targetSize << " elements" << std::endl;

		persistentVec.clear();
		std::cout << "  After clear: size=" << persistentVec.size() << ", capacity=" << persistentVec.capacity() << std::endl;

		std::cout << "  Reserving for " << targetSize << " floats (" << (targetSize * sizeof(float)) << " bytes)" << std::endl;
		persistentVec.reserve(targetSize);
		std::cout << "  After reserve: size=" << persistentVec.size() << ", capacity=" << persistentVec.capacity() << std::endl;

		for(size_t i = 0; i < targetSize; ++i)
		{
			persistentVec.push_back(static_cast<float>(i * 0.1f));
		}
		std::cout << "  After filling: size=" << persistentVec.size() << std::endl;
	}

	std::cout << "\nTest passed!" << std::endl;
}

void testSmallAllocatorMultipleVectors()
{
	std::cout << "\n=== Test: Multiple Vectors with SmallAllocator ===" << std::endl;

	SmallAllocator allocator;

	std::cout << "Creating and destroying multiple vectors..." << std::endl;

	for(int i = 0; i < 5; ++i)
	{
		std::cout << "\nIteration " << i << ":" << std::endl;

		Vector<float> vec1(allocator);
		vec1.reserve(200);
		for(int j = 0; j < 200; ++j)
			vec1.push_back(static_cast<float>(j));
		std::cout << "  vec1: size=" << vec1.size() << ", capacity=" << vec1.capacity() << std::endl;

		Vector<float> vec2(allocator);
		vec2.reserve(150);
		for(int j = 0; j < 150; ++j)
			vec2.push_back(static_cast<float>(j * 2));
		std::cout << "  vec2: size=" << vec2.size() << ", capacity=" << vec2.capacity() << std::endl;

		vec1.clear();
		std::cout << "  vec1 cleared: size=" << vec1.size() << ", capacity=" << vec1.capacity() << std::endl;

		vec1.reserve(300);
		std::cout << "  vec1 re-reserved: size=" << vec1.size() << ", capacity=" << vec1.capacity() << std::endl;
	}

	std::cout << "\nTest passed!" << std::endl;
}

void testSmallAllocatorGrowthPattern()
{
	std::cout << "\n=== Test: SmallAllocator Growth Pattern ===" << std::endl;

	SmallAllocator allocator;
	Vector<float> vec(allocator);

	std::cout << "Testing grow pattern that triggers fragmentation..." << std::endl;

	for(int cycle = 0; cycle < 20; ++cycle)
	{
		vec.clear();
		size_t targetSize = 50 + cycle * 100;
		std::cout << "Cycle " << cycle << ": reserve " << targetSize << " elements" << std::endl;
		vec.reserve(targetSize);

		for(size_t i = 0; i < targetSize; ++i)
		{
			vec.push_back(static_cast<float>(i));
		}
	}

	std::cout << "\nTest passed!" << std::endl;
}

int main()
{
	std::cout << "Starting allocator tests..." << std::endl;

	try
	{
		testSmallAllocatorFragmentation();
		testSmallAllocatorMultipleVectors();
		testSmallAllocatorGrowthPattern();

		std::cout << "\n=== ALL TESTS PASSED ===" << std::endl;
		return 0;
	}
	catch(const std::exception& e)
	{
		std::cerr << "\nTest failed with exception: " << e.what() << std::endl;
		return 1;
	}
	catch(...)
	{
		std::cerr << "\nTest failed with unknown exception" << std::endl;
		return 1;
	}
}
