#ifndef TSN_DGM_TRAVERSAL_H
#define TSN_DGM_TRAVERSAL_H

#include "transmission_operations.h"

namespace tsndgm {

enum TraversalDirection : std::uint8_t { FORWARD, BACKWARD };

using Vertex = const TransmissionOperation;
using MachineOperations = const LinkTransmissions;
struct Edge {
  Vertex *source;
  Vertex *target;
  EdgeType edge_type;
};

struct DFSVisitor {
  enum Event : std::uint8_t {
    DISCOVER_VERTEX,
    FINISH_VERTEX,
    EXAMINE_EDGE,
    TREE_EDGE,
    BACK_EDGE,
    FORWARD_OR_CROSS_EDGE,
    FINISH_EDGE
  };

  std::variant<Vertex *, Edge> visited_element;
  Event event;

  auto update(const DFSVisitor &new_visitor) -> DFSVisitor & {
    *this = new_visitor;
    return *this;
  }
  void print();
};

struct DFSTraversal {
  template <TraversalDirection D> struct VisitorState {
    struct Iterator {
      friend struct VisitorState;
      enum Type : std::uint8_t { FIFO, MACHINE, JOB, END };
      using IteratorCategory = std::forward_iterator_tag;
      using DifferenceType = std::ptrdiff_t;
      using ValueType = Vertex;
      using Pointer = Vertex *;
      using Reference = Vertex &;

      Iterator() = default;
      Iterator(const DFSTraversal *traversal, const VisitorState *state,
               Type type = FIFO)
          : state_(state), traversal_(traversal), type_(type) {
        setup(type);
      };

      auto operator*() -> Reference;
      auto operator->() -> Pointer;
      auto operator++() -> Iterator &;
      friend auto operator==(const Iterator &a, const Iterator &b) -> bool {
        if (a.type_ == MACHINE || a.type_ == END) {
          return a.type_ == b.type_;
        }
        return a.type_ == b.type_ && a.it_ == b.it_;
      }
      auto last() -> Vertex * { return last_; };
      auto type() -> EdgeType { return static_cast<EdgeType>(type_); };

    private:
      using VecIterator = std::vector<TransmissionOperation *>::const_iterator;
      const VisitorState *state_;
      const DFSTraversal *traversal_;
      Type type_;
      VecIterator it_, end_;
      Vertex *fifo_helper_, *last_;

      auto setup(Type type) -> Iterator &;
    };

    Vertex *op;
    MachineOperations *operations;
    LinkOpPosition pos;
    PCPValue pcp;
    Iterator it;

    VisitorState(const DFSTraversal *traversal, Vertex *op)
        : op(op), it(traversal, this, Iterator::JOB), traversal_(traversal) {};
    VisitorState(const DFSTraversal *traversal, MachineOperations &operations,
                 LinkOpPosition pos)
        : op(operations[pos]), operations(&operations), pos(pos),
          pcp(operations[pos]->pcp), it(traversal, this, Iterator::FIFO),
          traversal_(traversal) {};
    VisitorState(const VisitorState &other)
        : op(other.op), operations(other.operations), pos(other.pos),
          pcp(other.pcp), it(other.it) {
      it.state_ = this;
    }

    auto operator=(const VisitorState &) -> VisitorState &;
    auto operator++() -> VisitorState & {
      ++it;
      return *this;
    };
    auto end() -> Iterator { return Iterator(traversal_, this, Iterator::END); }

  private:
    const DFSTraversal *traversal_;
  };

  DFSTraversal() = default;
  DFSTraversal(const ProcessingOrder *processing_order,
               const OperationPosition *position);

  template <TraversalDirection D>
  [[nodiscard]] auto traverse(Vertex *start) -> Generator<DFSVisitor>;
  auto size() -> size_t { return processing_order_->total_operations; };

private:
  enum Color : std::uint8_t { WHITE, GRAY, BLACK };

  template <TraversalDirection D>
  [[nodiscard]] auto get_pcp_neighbor(Vertex &op) const -> Vertex *;

  const ProcessingOrder *processing_order_;
  const OperationPosition *position_;
  std::vector<Color> color_;
};

} // namespace tsndgm

#endif // TSN_DGM_TRAVERSAL_H
