#include <core/Containers.hpp>

int main()
{
	Ursa::Vector<int> sequence{1, 2};
	Ursa::Array<int, 2> fixed{{3, 4}};
	Ursa::Map<int, int> mapping;
	mapping[5] = 6;
	Ursa::Set<int> unique;
	unique.insert(7);
	Ursa::Deque<int> queue;
	queue.push_back(8);
	Ursa::String text("Ursa");

	return sequence[0] == 1 && fixed[1] == 4 && mapping[5] == 6 && *unique.begin() == 7 && queue.front() == 8 &&
	               text == "Ursa"
	           ? 0
	           : 1;
}
