// MIT License Copyright (c) 2016, Hao Wei.

#ifndef _UNITHEAP_H
#define _UNITHEAP_H

#include <cstdint>
#include <climits>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>

namespace gorder {

class ListElement {
public:
  int key;
  uint64_t prev;
  uint64_t next;
};

class HeadEnd {
public:
  uint64_t first;
  uint64_t second;
};

class UnitHeap {
public:
  std::vector<int> update;
  std::vector<ListElement> LinkedList; // key=degree, prev=id of previous node
                                       // in degree DESC ordering, next=...
  std::vector<HeadEnd> Header; // first=id of first node with this degree,
                               // second=id of last node...
  size_t heapsize = 0;         // updated at each instant
  uint64_t top;                      // id of node with highest degree
  uint64_t huge;                     // ignore huge nodes (deg > sqrt of graph size)
  uint64_t none;                     // integer that cannot be a node index
  const int infty = INT_MAX/2;

  // reserve memory
  UnitHeap(uint64_t size) {
   none = size + 2;
   huge = sqrt((double)size); // should be in ReConstruct with actual heapsize
   LinkedList.resize(size, {.key=infty,.prev=none,.next=none});
   update.resize(size, infty);
  }

  // prepare insertion of node by compensating the key with a negative update
  void InsertElement(const uint64_t index, const int key) {
    if(key < 0) { exit(0); }
    LinkedList[index].key = key;//+1;
    update[index] = -key;
    heapsize++;
  }
  // oncle all elements are inserted, create headers and linked list
  void ReConstruct() {
    std::vector<uint64_t> g(heapsize);
    for (uint64_t i = 0; i < heapsize; i++)
      g[i] = i;

    std::sort(g.begin(), g.end(), [&](const uint64_t a, const uint64_t b) -> bool {
      return LinkedList[a].key > LinkedList[b].key or (LinkedList[a].key == LinkedList[b].key and a < b);
    }); // degree DESC

    top = g[0];
    int current_key = LinkedList[top].key; // max degree
    Header.resize(10*current_key + 1, {.first=none,.second=none});
    Header[current_key].first = top;
    for (size_t i = 0; i < g.size(); i++) {
      uint64_t v = g[i];
      LinkedList[v].prev = (i>0) ? g[i-1] : none;
      LinkedList[v].next = (i<g.size()-1) ? g[i+1] : none;

      int key = LinkedList[g[i]].key;
      if (key != current_key) {
        Header[current_key].second = g[i - 1];
        Header[key].first = g[i];
        current_key = key;
      }
    }

    Header[current_key].second = g.back();
  }

  // update headers when element is removed
  void erase_key_element(const uint64_t index, const uint64_t next, const uint64_t prev) {
    int key = LinkedList[index].key;
    if (Header[key].first == Header[key].second) // == index
      Header[key].first = Header[key].second = none;
    else if (index == Header[key].first)
      Header[key].first = next;
    else if (index == Header[key].second)
      Header[key].second = prev;
  }

  // find best element and output it
  uint64_t ExtractMax() {
    uint64_t tmptop;
    do {
      tmptop = top;
      if (update[top] < 0) DecreaseTop();
    } while (top != tmptop);

    DeleteElement(top);
    return tmptop;
  }
  // update top and decrease it if necessary
  void DecreaseTop() {
    // If the "top" has to be updated, try and update half what you should
    // and see if someone else comes on top
    // Recursion will take care of further updates

    const uint64_t next = LinkedList[top].next;
    if(next == none) {
      return;
    }

    const int key = LinkedList[top].key;
    const int leftover = update[top] / 2;
    const int new_key = key + update[top] - leftover;
    if(-update[top] > key) { exit(0);}
    if (new_key >= LinkedList[next].key) return;
    update[top] = leftover;


    uint64_t level_tail = Header[key].second;
    uint64_t next_level = LinkedList[level_tail].next;
    uint64_t loops = 0;
    while (next_level != none && LinkedList[next_level].key >= new_key) {
      level_tail = Header[LinkedList[next_level].key].second;
      next_level = LinkedList[level_tail].next;
    }

    LinkedList[next].prev = none;
    LinkedList[top].prev = level_tail;
    LinkedList[top].next = next_level;
    LinkedList[level_tail].next = top;
    if (next_level != none) LinkedList[next_level].prev = top;

    erase_key_element(top, next, none);

    if(new_key < 0) { exit(0);}
    LinkedList[top].key = new_key;

    Header[new_key].second = top;
    if (Header[new_key].first == none)
      Header[new_key].first = top;

    top = next;
  }

  // delete a node from linked list, update headers
  void DeleteElement(const uint64_t index) {
    update[index] = infty;
    uint64_t prev = LinkedList[index].prev;
    uint64_t next = LinkedList[index].next;

    if (prev != none) LinkedList[prev].next = next;
    if (next != none) LinkedList[next].prev = prev;

    erase_key_element(index, next, prev);

    if (top == index)
      top = next;
    LinkedList[index].prev = LinkedList[index].next = none;
    heapsize --;
  }

  // change element key: increase update if possible otherwise call IncrementKey
  void lazyIncrement(const uint64_t index, const int up) {
    if (update[index] == infty) return;
    if (update[index] == 0 and up > 0)
      IncrementKey(index); // unlikely
    else {
      update[index] += up; // up can be negative
      if(-update[index] > LinkedList[index].key) { exit(0);}
    }
  }
  // increment key, move element in linked list, update headers
  void IncrementKey(const uint64_t index) {
    const uint64_t level_head = Header[LinkedList[index].key].first;
    const uint64_t prev = LinkedList[index].prev;
    const uint64_t next = LinkedList[index].next;

    if (level_head != index) { // if index is head of level, keep LinkedList
      LinkedList[prev].next = next;
      if (next != none)
        LinkedList[next].prev = prev;

      uint64_t prev_level = LinkedList[level_head].prev;
      LinkedList[index].prev = prev_level;
      LinkedList[index].next = level_head;
      LinkedList[level_head].prev = index;
      if (prev_level != none)
        LinkedList[prev_level].next = index;
    }

    erase_key_element(index, next, prev);

    int key = ++LinkedList[index].key;
    Header[key].second = index;
    if (Header[key].first == none) {
      Header[key].first = index;
      if (key > LinkedList[top].key) top = index; // was not in the if initially
    }

    if (key + 4 >= (int)Header.size())
      Header.resize(Header.size() * 1.5, {.first=none,.second=none});
  }
};

}  // namespace gorder

#endif
