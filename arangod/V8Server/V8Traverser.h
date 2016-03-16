////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2016 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Michael Hackstein
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGOD_V8_SERVER_V8_TRAVERSER_H
#define ARANGOD_V8_SERVER_V8_TRAVERSER_H 1

#include "Utils/ExplicitTransaction.h"
#include "VocBase/edge-collection.h"
#include "VocBase/ExampleMatcher.h"
#include "VocBase/Traverser.h"

namespace arangodb {

struct OperationCursor;

namespace velocypack {
class Slice;
}

class Transaction;
}

class VocShaper;

struct EdgeInfo {
  TRI_voc_cid_t cid;
  TRI_doc_mptr_t mptr;

  EdgeInfo(TRI_voc_cid_t pcid, TRI_doc_mptr_t& pmptr)
      : cid(pcid), mptr(pmptr) {}

  bool operator==(EdgeInfo const& other) const {
    if (cid == other.cid && mptr.getHash() == other.mptr.getHash()) {
      // We have to look into the key now. The only source of truth.
      char const* l = TRI_EXTRACT_MARKER_KEY(&mptr);
      char const* r = TRI_EXTRACT_MARKER_KEY(&other.mptr);
      return strcmp(l, r) == 0;
    }
    return false;
  }
};

////////////////////////////////////////////////////////////////////////////////
/// @brief typedef the template instantiation of the PathFinder
////////////////////////////////////////////////////////////////////////////////

typedef arangodb::basics::PathFinder<std::string, std::string, double>
    ArangoDBPathFinder;

typedef arangodb::basics::ConstDistanceFinder<
    std::string, std::string> ArangoDBConstDistancePathFinder;

namespace arangodb {
namespace traverser {

// A collection of shared options used in several functions.
// Should not be used directly, use specialization instead.
struct BasicOptions {
 protected:
  std::unordered_map<std::string, arangodb::ExampleMatcher*> _edgeFilter;
  std::unordered_map<std::string, arangodb::ExampleMatcher*> _vertexFilter;

  BasicOptions() : useEdgeFilter(false), useVertexFilter(false) {}

  ~BasicOptions() {
    // properly clean up the mess
    for (auto& it : _edgeFilter) {
      delete it.second;
    }
    for (auto& it : _vertexFilter) {
      delete it.second;
    }
  }

 public:
  std::string start;
  bool useEdgeFilter;
  bool useVertexFilter;

  void addEdgeFilter(v8::Isolate* isolate, v8::Handle<v8::Value> const& example,
                     std::string const&,
                     std::string& errorMessage);

  void addEdgeFilter(arangodb::velocypack::Slice const& example,
                     std::string const&);

  void addVertexFilter(v8::Isolate* isolate,
                       v8::Handle<v8::Value> const& example,
                       arangodb::Transaction* trx,
                       std::string const&,
                       std::string& errorMessage);

  bool matchesEdge(arangodb::velocypack::Slice edge) const;

  bool matchesVertex(std::string const& v) const;
};

struct NeighborsOptions : BasicOptions {
 private:
  std::unordered_set<TRI_voc_cid_t> _explicitCollections;

 public:
  TRI_edge_direction_e direction;
  uint64_t minDepth;
  uint64_t maxDepth;

  NeighborsOptions() : direction(TRI_EDGE_OUT), minDepth(1), maxDepth(1) {}

  bool matchesVertex(std::string const&) const;

  void addCollectionRestriction(TRI_voc_cid_t cid);
};

struct ShortestPathOptions : BasicOptions {
 public:
  std::string direction;
  bool useWeight;
  std::string weightAttribute;
  double defaultWeight;
  bool bidirectional;
  bool multiThreaded;
  std::string end;

  ShortestPathOptions()
      : direction("outbound"),
        useWeight(false),
        weightAttribute(""),
        defaultWeight(1),
        bidirectional(true),
        multiThreaded(true) {}

  bool matchesVertex(std::string const&) const;
};

class SingleServerTraversalPath : public TraversalPath {
 public:
  explicit SingleServerTraversalPath(
      arangodb::basics::EnumeratedPath<std::string, std::string> const& path)
      : _path(path) {}

  ~SingleServerTraversalPath() {}

  void pathToVelocyPack(arangodb::Transaction*,
                        arangodb::velocypack::Builder&) override;

  void lastEdgeToVelocyPack(arangodb::Transaction*,
                            arangodb::velocypack::Builder&) override;

  void lastVertexToVelocyPack(arangodb::Transaction*,
                              arangodb::velocypack::Builder&) override;

 private:

  void getDocumentByIdentifier(Transaction*, std::string const&,
                               arangodb::velocypack::Builder&);

  arangodb::basics::EnumeratedPath<std::string, std::string> _path;

  arangodb::velocypack::Builder _searchBuilder;

};

class DepthFirstTraverser : public Traverser {

 private:
  //////////////////////////////////////////////////////////////////////////////
  /// @brief callable class to load edges based on opts.
  //////////////////////////////////////////////////////////////////////////////

  class EdgeGetter {
   public:
    EdgeGetter(DepthFirstTraverser* traverser,
                        TraverserOptions const& opts,
                        CollectionNameResolver* resolver, Transaction* trx)
        : _traverser(traverser), _resolver(resolver), _opts(opts), _trx(trx) {}

    //////////////////////////////////////////////////////////////////////////////
    /// @brief Function to fill the list of edges properly.
    //////////////////////////////////////////////////////////////////////////////

    void operator()(std::string const&, std::vector<std::string>&,
                    TRI_doc_mptr_t*&, size_t&, bool&);

   private:

    //////////////////////////////////////////////////////////////////////////////
    /// @brief Get an edge index for the given collection by name
    //////////////////////////////////////////////////////////////////////////////
    
    EdgeIndex* getEdgeIndex(std::string const&, TRI_voc_cid_t&);

    //////////////////////////////////////////////////////////////////////////////
    /// @brief Collection name resolver
    //////////////////////////////////////////////////////////////////////////////
    DepthFirstTraverser* _traverser;

    //////////////////////////////////////////////////////////////////////////////
    /// @brief Collection name resolver
    //////////////////////////////////////////////////////////////////////////////

    CollectionNameResolver* _resolver;

    //////////////////////////////////////////////////////////////////////////////
    /// @brief Cache for indexes. Maps collectionName to Index
    //////////////////////////////////////////////////////////////////////////////

    std::unordered_map<std::string, std::pair<TRI_voc_cid_t, EdgeIndex*>>
        _indexCache;

    //////////////////////////////////////////////////////////////////////////////
    /// @brief Traverser options
    //////////////////////////////////////////////////////////////////////////////

    TraverserOptions _opts;

    //////////////////////////////////////////////////////////////////////////////
    /// @brief Pointer to active transaction
    ///        All Edge Collections have to be properly locked before traversing!
    //////////////////////////////////////////////////////////////////////////////
    Transaction* _trx;
  };

  //////////////////////////////////////////////////////////////////////////////
  /// @brief collection name resolver
  //////////////////////////////////////////////////////////////////////////////

  CollectionNameResolver* _resolver;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief internal cursor to enumerate the paths of a graph
  //////////////////////////////////////////////////////////////////////////////

  std::unique_ptr<arangodb::basics::PathEnumerator<std::string, std::string,
                                                   TRI_doc_mptr_t>> _enumerator;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief internal getter to extract an edge
  //////////////////////////////////////////////////////////////////////////////

  EdgeGetter _edgeGetter;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief internal function to extract vertex information
  //////////////////////////////////////////////////////////////////////////////

  std::function<bool(std::string const&, std::string const&,
                     size_t, std::string&)> _getVertex;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief a vector containing all required edge collection structures
  //////////////////////////////////////////////////////////////////////////////

  std::vector<TRI_document_collection_t*> _edgeCols;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief Outer top level transaction
  //////////////////////////////////////////////////////////////////////////////

  Transaction* _trx;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief internal function to define the _getVertex and _getEdge functions
  //////////////////////////////////////////////////////////////////////////////

  void _defInternalFunctions();

 public:
  DepthFirstTraverser(
      std::vector<TRI_document_collection_t*> const&, TraverserOptions&,
      CollectionNameResolver*, Transaction*,
      std::unordered_map<size_t, std::vector<TraverserExpression*>> const*);

  //////////////////////////////////////////////////////////////////////////////
  /// @brief Reset the traverser to use another start vertex
  //////////////////////////////////////////////////////////////////////////////

  void setStartVertex(arangodb::velocypack::Slice const& v) override;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief Get the next possible path in the graph.
  //////////////////////////////////////////////////////////////////////////////

  TraversalPath* next() override;

 private:
  bool edgeMatchesConditions(TRI_doc_mptr_t&, size_t&, size_t);

  bool vertexMatchesConditions(arangodb::velocypack::Slice const&, size_t);
};
}
}

////////////////////////////////////////////////////////////////////////////////
/// @brief callback to weight an edge
////////////////////////////////////////////////////////////////////////////////

typedef std::function<double(arangodb::velocypack::Slice const)>
    WeightCalculatorFunction;

////////////////////////////////////////////////////////////////////////////////
/// @brief Information required internally of the traverser.
///        Used to easily pass around collections.
///        Also offer abstraction to extract edges.
////////////////////////////////////////////////////////////////////////////////

class EdgeCollectionInfo {
 private:
  //////////////////////////////////////////////////////////////////////////////
  /// @brief the underlying transaction
  //////////////////////////////////////////////////////////////////////////////

  arangodb::Transaction* _trx;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief edge collection name
  //////////////////////////////////////////////////////////////////////////////

  std::string _collectionName;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief index id
  //////////////////////////////////////////////////////////////////////////////

  std::string _indexId;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief VPackBuilder to build edge index search value in place.
  ///        Reused for every request.
  //////////////////////////////////////////////////////////////////////////////

  arangodb::velocypack::Builder _searchBuilder;

  WeightCalculatorFunction _weighter;

 public:
  EdgeCollectionInfo(arangodb::Transaction* trx,
                     std::string const& collectionName,
                     WeightCalculatorFunction weighter);

////////////////////////////////////////////////////////////////////////////////
/// @brief Get edges for the given direction and start vertex.
////////////////////////////////////////////////////////////////////////////////

  arangodb::OperationCursor getEdges(TRI_edge_direction_e direction,
                                     std::string const&);

  double weightEdge(arangodb::velocypack::Slice const);

////////////////////////////////////////////////////////////////////////////////
/// @brief Return name of the wrapped collection
////////////////////////////////////////////////////////////////////////////////

  std::string const& getName(); 

};

////////////////////////////////////////////////////////////////////////////////
/// @brief Wrapper for the shortest path computation
////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<ArangoDBPathFinder::Path> TRI_RunShortestPathSearch(
    std::vector<EdgeCollectionInfo*>& collectionInfos,
    arangodb::traverser::ShortestPathOptions& opts);

std::unique_ptr<ArangoDBConstDistancePathFinder::Path>
TRI_RunSimpleShortestPathSearch(
    std::vector<EdgeCollectionInfo*>& collectionInfos,
    arangodb::Transaction*,
    arangodb::traverser::ShortestPathOptions& opts);

////////////////////////////////////////////////////////////////////////////////
/// @brief Wrapper for the neighbors computation
////////////////////////////////////////////////////////////////////////////////

void TRI_RunNeighborsSearch(
    std::vector<EdgeCollectionInfo*>& collectionInfos,
    arangodb::traverser::NeighborsOptions& opts,
    std::unordered_set<std::string>& distinct);

#endif
