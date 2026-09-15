#include <cstddef>
#include <iostream>
#include <stdexcept>

#include "../../Engine/utility/smallarray.h"

static void require(bool ok, const char* message) {
  if(!ok)
    throw std::runtime_error(message);
  }

int main() {
  try {
    // Exercise both sides of each node boundary, repeated clear and reuse, and destruction after clear.
    for(size_t count : {size_t(0),size_t(1),size_t(31),size_t(32),size_t(33),size_t(64),size_t(65),size_t(97)}) {
      Tempest::Detail::SmallList<size_t,32> list;
      for(size_t cycle=0;cycle<4;++cycle) {
        require(list.size()==0,"Cleared list is not empty");
        require(list.begin()->next==nullptr,"Cleared list retains a deleted node");
        for(size_t i=0;i<count;++i) {
          list.push(i+cycle*100);
          require(list.last()==i+cycle*100,"Reuse writes to the wrong node");
          }
        require(list.size()==count,"Incorrect list size");
        const auto* node=list.begin();
        size_t index=0;
        while(index<count) {
          require(node!=nullptr,"Missing overflow node");
          for(size_t j=0;j<list.chunkSize && index<count;++j,++index)
            require(node->val[j]==index+cycle*100,"Incorrect value in overflow node");
          node=node->next;
          }
        list.clear();
        list.clear();
        }
      }
    std::cout << "SmallList boundary, clear, reuse and destruction checks passed\n";
    }
  catch(const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
    }
  }
