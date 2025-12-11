#pragma once

#include "barock/core/point.hpp"
#include "jsl/optional.hpp"
#include <array>
#include <cassert>
#include <iostream>
#include <utility>
#include <vector>

namespace barock {
  template<typename _Scalar, typename _Data>
  struct node_t {
    point_t<_Scalar> point;
    _Data            value;

    template<typename... _Args>
    node_t(const point_t<_Scalar> &point, _Args &&...args)
      : point(point)
      , value(std::forward<_Args>(args)...) {}
  };

  template<typename _Scalar, typename _Data>
  class quad_tree_t {
    enum { eTopLeft = 0, eTopRight, eBottomRight, eBottomLeft };
    using node = node_t<_Scalar, _Data>;

    point_t<_Scalar>                    min_{}, max_{};
    bool                                divided_ = false;
    std::array<quad_tree_t *, 4>        leaves_{ nullptr, nullptr, nullptr, nullptr };
    std::vector<node_t<_Scalar, _Data>> objects_;

    public:
    constexpr static size_t  SPLIT_THRESHOLD = 4;
    constexpr static _Scalar MIN_CELL_SIZE   = static_cast<_Scalar>(6);

    quad_tree_t(const point_t<_Scalar> &min, const point_t<_Scalar> &size)
      : min_(min)
      , max_(min + size) {}

    ~quad_tree_t() {
      if (divided_) {
        for (auto leaf : leaves_) {
          delete leaf;
        }
      }
    }

    bool
    responsible(point_t<_Scalar> point) {
      return point >= min_ && point < max_;
    }

    template<typename... Args>
    void
    insert(Args &&...args) {
      node value = node(std::forward<Args>(args)...);

      if (!responsible(value.point))
        return;

      if (divided_) {
        for (auto &leaf : leaves_) {
          if (leaf->responsible(value.point)) {
            leaf->insert(std::move(value));
            return;
          }
        }
        // This is an error, we should have exit before
        std::cerr << "Failed to distribute " << value.point.x << ", " << value.point.y
                  << " on tree " << min_.x << ", " << min_.y << " | " << max_.x << ", " << max_.y
                  << "\n";
        assert(false && "Failed to distribute node on leaves");
        return;
      }

      objects_.emplace_back(std::move(value));

      // Perform subdivision once threshold is hit
      if (objects_.size() >= SPLIT_THRESHOLD) {
        if ((max_.x - min_.x) <= MIN_CELL_SIZE || (max_.y - min_.y) <= MIN_CELL_SIZE) {
          // Cannot subdivide further
          return;
        }

        int w = max_.x - min_.x;
        int h = max_.y - min_.y;

        int w1 = w / 2;
        int w2 = w - w1;
        int h1 = h / 2;
        int h2 = h - h1;

        point_t<_Scalar> tl_size{ w1, h1 };
        point_t<_Scalar> tr_size{ w2, h1 };
        point_t<_Scalar> bl_size{ w1, h2 };
        point_t<_Scalar> br_size{ w2, h2 };

        // Top left
        leaves_[eTopLeft] = new quad_tree_t(point_t<_Scalar>{ min_.x, min_.y }, tl_size);

        // Top right
        leaves_[eTopRight] = new quad_tree_t(point_t<_Scalar>{ min_.x + w1, min_.y }, tr_size);

        // Bottom right
        leaves_[eBottomRight] =
          new quad_tree_t(point_t<_Scalar>{ min_.x + w1, min_.y + h1 }, br_size);

        // Bottom left
        leaves_[eBottomLeft] = new quad_tree_t(point_t<_Scalar>{ min_.x, min_.y + h1 }, bl_size);

        divided_ = true;

        // Split objects onto our leaves
        for (auto it = objects_.begin(); it != objects_.end();) {
          auto &node = *it;
          for (auto &leaf : leaves_) {
            if (leaf->responsible(node.point)) {
              leaf->insert(std::move(node));
              it = objects_.erase(it);
              // Break the inner loop, found our leaf that is now
              // responsible for `node'
              goto next;
            }
          }
          it++;
        next:
        }

        assert(objects_.size() == 0);
        objects_.clear();
      }
    };

    std::vector<const node *>
    query(const point_t<_Scalar> &min, const point_t<_Scalar> &max) const {
      std::vector<const node *> nodes;

      if (!divided_) {
        // Insert nodes held by this node
        for (auto &node : objects_) {
          if (node.point >= min && node.point <= max) {
            nodes.push_back(&node);
          }
        }
      } else {
        for (auto &leaf : leaves_) {
          auto child_nodes = leaf->query(min, max);
          nodes.insert(nodes.end(), child_nodes.begin(), child_nodes.end());
        }
      }
      return nodes;
    }

    bool
    divided() const {
      return divided_;
    }

    point_t<_Scalar>
    min() const {
      return min_;
    }
    point_t<_Scalar>
    max() const {
      return max_;
    }

    void
    clear() {
      objects_.clear();
      if (divided_)
        for (auto leaf : leaves_)
          leaf->clear();
    }
  };
};
