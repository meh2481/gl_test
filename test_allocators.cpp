#include "src/memory/SmallAllocator.h"
#include "src/memory/LargeMemoryAllocator.h"
#include "src/core/Vector.h"
#include <iostream>
#include <cassert>

void testSmallAllocatorFragmentation()
{
	std::cout << "\n=== Test: SmallAllocator Fragmentation (Reproduce SceneManager crash) ===" << std::endl;

	SmallAllocator allocator;

	// Create multiple persistent vectors like in real scene
	Vector<float> debugLineData(allocator);
	Vector<float> otherData1(allocator);
	Vector<float> otherData2(allocator);

	std::cout << "Simulating SceneManager pattern with multiple vectors and extreme fragmentation..." << std::endl;

	// Simulate many frame cycles with varying sizes to create fragmentation
	for(int frame = 0; frame < 100; ++frame)
	{
		// Vary size to create fragmentation
		size_t size1 = 100 + (frame % 7) * 200;
		size_t size2 = 150 + (frame % 5) * 150;
		size_t size3 = 200 + (frame % 3) * 300;

		if (frame % 10 == 0) {
			std::cout << "\nFrame " << frame << ": sizes=" << size1 << "," << size2 << "," << size3 << std::endl;
		}

		// Clear and reserve in different patterns
		debugLineData.clear();
		debugLineData.reserve(size1);
		for(size_t i = 0; i < size1; ++i) {
			debugLineData.push_back(static_cast<float>(i));
		}

		otherData1.clear();
		otherData1.reserve(size2);
		for(size_t i = 0; i < size2; ++i) {
			otherData1.push_back(static_cast<float>(i * 2));
		}

		otherData2.clear();
		otherData2.reserve(size3);
		for(size_t i = 0; i < size3; ++i) {
			otherData2.push_back(static_cast<float>(i * 3));
		}
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
