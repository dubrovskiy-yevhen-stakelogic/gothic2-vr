#pragma once

#include <Tempest/AbstractGraphicsApi>

namespace Tempest {

class Device;

class Fence final {
  public:
    Fence() = default;
    Fence(Fence&& f)=default;
    ~Fence();
    Fence& operator = (Fence&& other)=default;

    void wait();
    bool wait(uint64_t time);
    // Retain a completion token without borrowing another Fence object's lifetime.
    Fence share() const;
    bool isEmpty() const { return impl==nullptr; }

  private:
    Fence(std::shared_ptr<AbstractGraphicsApi::Fence>& f);

    std::shared_ptr<AbstractGraphicsApi::Fence> impl;

  friend class Tempest::Device;
  };
}
