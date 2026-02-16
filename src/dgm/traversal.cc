#include "traversal.h"
#include "transmission_operations.h"
#include "utils/generator.h"
#include <algorithm>
#include <cassert>
#include <deque>
#include <print>
#include <ranges>
#include <string>
#include <utility>

namespace tsndgm {

template <TraversalDirection D>
auto DFSTraversal::VisitorState<D>::Iterator::operator*()
    -> DFSTraversal::VisitorState<D>::Iterator::Reference {
  last_ = nullptr;
  switch (type_) {
  [[likely]] case FIFO:
    if constexpr (D == BACKWARD) {
      last_ = fifo_helper_;
    } else {
      last_ = *it_;
    }
    break;
  [[unlikely]] case MACHINE:
    if constexpr (D == BACKWARD) {
      last_ = (*state_->operations)[state_->pos - 1];
    } else {
      last_ = (*state_->operations)[state_->pos + 1];
    }
    break;
  [[likely]] case JOB:
    last_ = *it_;
    break;
  default:
    std::unreachable();
  }
  return *last_;
}

template <TraversalDirection D>
auto DFSTraversal::VisitorState<D>::Iterator::operator++()
    -> DFSTraversal::VisitorState<D>::Iterator & {
  switch (type_) {
  [[likely]] case FIFO:
    if constexpr (D == BACKWARD) {
      while (++it_ != end_) {
        fifo_helper_ = traversal_->get_pcp_neighbor<D>(**it_);
        if (fifo_helper_ != nullptr) {
          return *this;
        }
      }
      return setup(MACHINE);
    } else {
      if (it_ != end_) {
        ++it_;
      } else {
        return setup(MACHINE);
      }
    }
    return *this;
  [[unlikely]] case MACHINE:
    return setup(JOB);
  [[likely]] case JOB:
    if (++it_ == end_) {
      setup(END);
    }
    return *this;
  default:
    std::unreachable();
  }
}

template <>
auto DFSTraversal::VisitorState<FORWARD>::Iterator::setup(Type type)
    -> DFSTraversal::VisitorState<FORWARD>::Iterator & {
  switch (type) {
  [[likely]] case FIFO: {
    type_ = FIFO;
    const auto *pcp_successor = traversal_->get_pcp_neighbor<FORWARD>(*state_->op);
    if (pcp_successor != nullptr) {
      it_ = pcp_successor->route_pred.cbegin();
      end_ = pcp_successor->route_pred.cend();
      return *this;
    }
    return setup(MACHINE);
  }
  [[unlikely]] case MACHINE:
    type_ = MACHINE;
    if (state_->pos < state_->operations->size() - 1) {
      return *this;
    }
    return setup(JOB);
  [[likely]] case JOB:
    type_ = JOB;
    if (state_->op->id == SINK_ID) {
      return setup(END);
    }
    assert(!state_->op->route_succ.empty());
    it_ = state_->op->route_succ.begin();
    end_ = state_->op->route_succ.end();
    return *this;
  [[unlikely]] case END:
    type_ = END;
    return *this;
  default:
    std::unreachable();
  }
}

template <>
auto DFSTraversal::VisitorState<BACKWARD>::Iterator::setup(Type type)
    -> DFSTraversal::VisitorState<BACKWARD>::Iterator & {
  switch (type) {
  [[likely]] case FIFO:
    type_ = FIFO;
    it_ = state_->op->route_succ.begin();
    end_ = state_->op->route_succ.end();
    for (; it_ != end_; ++it_) {
      fifo_helper_ = traversal_->get_pcp_neighbor<BACKWARD>(**it_);
      if (fifo_helper_ != nullptr) {
        return *this;
      }
    }
    return setup(MACHINE);
  [[unlikely]] case MACHINE:
    type_ = MACHINE;
    if (state_->pos > 0) {
      return *this;
    }
    return setup(JOB);
  [[likely]] case JOB:
    type_ = JOB;
    if (state_->op->id == SOURCE_ID) {
      return setup(END);
    }
    assert(!state_->op->route_pred.empty());
    it_ = state_->op->route_pred.begin();
    end_ = state_->op->route_pred.end();
    return *this;
  [[unlikely]] case END:
    type_ = END;
    return *this;
  default:
    std::unreachable();
  }
}

template <TraversalDirection D>
auto DFSTraversal::VisitorState<D>::operator=(const DFSTraversal::VisitorState<D> &other)
    -> DFSTraversal::VisitorState<D> & {
  if (this == &other) {
    return *this;
  }

  op = other.op;
  operations = other.operations;
  pos = other.pos;
  pcp = other.pcp;
  it = other.it;
  it.state_ = this;

  return *this;
}

DFSTraversal::DFSTraversal(const ProcessingOrder *processing_order,
                           const OperationPosition *position)
    : processing_order_(processing_order), position_(position),
      color_(processing_order->total_operations) {}

template <TraversalDirection D> auto DFSTraversal::get_pcp_neighbor(Vertex &op) const -> Vertex * {
  if (op.id <= SINK_ID) {
    return nullptr;
  }

  auto [operations, pos] = (*position_)[op.id];
  if constexpr (D == BACKWARD) {
    auto it = std::ranges::find_if(
        (*operations | std::views::reverse | std::views::drop(operations->size() - pos)),
        [&](auto &op1) -> auto { return op1->pcp == op.pcp; });
    return it == operations->crend() ? nullptr : *it;
  } else {
    auto it = std::ranges::find_if((*operations | std::views::drop(pos)),
                                   [&](auto &op1) -> auto { return op1->pcp == op.pcp; });
    return it == operations->cend() ? nullptr : *it;
  }
}

template <TraversalDirection D>
auto DFSTraversal::traverse(Vertex *start) -> Generator<DFSVisitor> {
  std::ranges::fill(color_, WHITE);
  DFSVisitor visitor;

  color_[start->id] = GRAY;
  std::deque<VisitorState<D>> stack = {VisitorState<D>(this, start)};
  co_yield visitor.update({.visited_element = start, .event = DFSVisitor::DISCOVER_VERTEX});

  while (!stack.empty()) {
    VisitorState<D> next = stack.front();
    stack.pop_front();

    if (next.tree_edge) {
      co_yield visitor.update({*next.tree_edge, DFSVisitor::FINISH_EDGE});
    }

    while (next.it != next.end()) {
      Vertex *v = &(*next.it);
      EdgeType const type = next.it.type();
      Edge e = D == BACKWARD ? Edge(v, next.op, type) : Edge(next.op, v, type);
      co_yield visitor.update({.visited_element = e, .event = DFSVisitor::EXAMINE_EDGE});

      switch (color_[v->id]) {
      [[likely]] case WHITE: {
        co_yield visitor.update({.visited_element = e, .event = DFSVisitor::TREE_EDGE});

        next.tree_edge = e;
        stack.push_front(++next);
        color_[v->id] = GRAY;

        co_yield visitor.update({.visited_element = v, .event = DFSVisitor::DISCOVER_VERTEX});
        if (v->id <= SINK_ID) {
          next = VisitorState<D>(this, v);
        } else {
          auto [operations, op_index] = (*position_)[v->id];
          next = VisitorState<D>(this, *operations, op_index);
        }
        break;
      }
      [[unlikely]] case GRAY:
        co_yield visitor.update({.visited_element = e, .event = DFSVisitor::BACK_EDGE});
        co_yield visitor.update({.visited_element = e, .event = DFSVisitor::FINISH_EDGE});
        ++next;
        break;
      [[unlikely]] case BLACK:
        co_yield visitor.update({.visited_element = e, .event = DFSVisitor::FORWARD_OR_CROSS_EDGE});
        co_yield visitor.update({.visited_element = e, .event = DFSVisitor::FINISH_EDGE});
        ++next;
        break;
      default:
        std::unreachable();
      }
    }
    color_[next.op->id] = BLACK;
    co_yield visitor.update({next.op, DFSVisitor::FINISH_VERTEX});
  }
}

void DFSVisitor::print() {
  std::string s;
  switch (event) {
  case DFSVisitor::DISCOVER_VERTEX:
    s = "discover";
  case DFSVisitor::FINISH_VERTEX: {
    Vertex *v = std::get<Vertex *>(visited_element);
    s = s.empty() ? "finish" : s;
    if (v->id > SINK_ID) {
      auto f = *v->frames.begin();
      std::println("{}: {} ([{},{}], {}#{})", s, v->id, v->source->id, v->target->id,
                   f.stream->name, f.id);
    } else {
      std::println("{}: {}", s, v->id);
    }
    break;
  }
  case DFSVisitor::EXAMINE_EDGE:
    s = "examine";
  case DFSVisitor::TREE_EDGE:
    s = s.empty() ? "tree" : s;
  case DFSVisitor::BACK_EDGE:
    s = s.empty() ? "back" : s;
  case DFSVisitor::FORWARD_OR_CROSS_EDGE:
    s = s.empty() ? "forward" : s;
  case DFSVisitor::FINISH_EDGE: {
    Edge const e = std::get<Edge>(visited_element);
    s = s.empty() ? "finish" : s;
    std::println("{}: ({}, {})", s, e.source->id, e.target->id);
    break;
  }
  default:
    break;
  }
}

template Generator<DFSVisitor> DFSTraversal::traverse<FORWARD>(Vertex *start);
template Generator<DFSVisitor> DFSTraversal::traverse<BACKWARD>(Vertex *start);

} // namespace tsndgm
