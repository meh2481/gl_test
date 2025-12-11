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

void testSmallAllocatorRealisticScenePattern()
{
	std::cout << "\n=== Test: Realistic SceneManager Debug Line Pattern ===" << std::endl;

	SmallAllocator allocator;
	Vector<float> debugLineData(allocator);

	std::cout << "Simulating real SceneManager pattern: many clear/reserve cycles with variable sizes..." << std::endl;

	// Simulate hundreds of frames with varying debug vertex counts
	// This matches the actual SceneManager::updateActiveScene() pattern on line 257-258
	for(int frame = 0; frame < 500; ++frame)
	{
		// Simulate variable number of debug vertices per frame (like physics debug drawing)
		// debugLineVerts.size() can vary significantly frame-to-frame
		size_t vertCount = 50 + (frame * 17) % 300;  // Varies from 50 to 350
		size_t floatCount = vertCount * 6;  // Each vertex has 6 floats (x, y, r, g, b, a)

		if (frame % 50 == 0) {
			std::cout << "Frame " << frame << ": " << vertCount << " verts = " << floatCount << " floats (" << (floatCount * sizeof(float)) << " bytes)" << std::endl;
		}

		// This is the EXACT pattern from SceneManager.cpp lines 257-266
		debugLineData.clear();
		debugLineData.reserve(floatCount);  // LINE 258 - THIS IS WHERE IT CRASHES

		for(size_t i = 0; i < floatCount; ++i) {
			debugLineData.push_back(static_cast<float>(i));
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
		testSmallAllocatorRealisticScenePattern();

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
