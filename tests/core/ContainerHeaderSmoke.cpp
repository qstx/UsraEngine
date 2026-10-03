#include <core/Containers.hpp>

int main() {
    ursa::Vector<int> sequence{1, 2};
    ursa::Array<int, 2> fixed{{3, 4}};
    ursa::Map<int, int> mapping;
    mapping[5] = 6;
    ursa::Set<int> unique;
    unique.insert(7);
    ursa::Deque<int> queue;
    queue.push_back(8);
    ursa::String text("Ursa");

    return sequence[0] == 1 && fixed[1] == 4 && mapping[5] == 6 && *unique.begin() == 7 && queue.front() == 8 &&
                   text == "Ursa"
               ? 0
               : 1;
}
