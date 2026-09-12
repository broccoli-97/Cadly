#include "OcctStepTransfer.h"

#include "OcctProgressBridge.h"

#include <Interface_EntityIterator.hxx>
#include <Interface_Graph.hxx>
#include <Interface_Static.hxx>
#include <Message_ProgressScope.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPControl_ActorRead.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <StepData_StepModel.hxx>
#include <StepRepr_RepresentationContext.hxx>
#include <StepShape_ManifoldSolidBrep.hxx>
#include <StepShape_NonManifoldSurfaceShapeRepresentation.hxx>
#include <StepShape_ShapeRepresentation.hxx>
#include <StepShape_TopologicalRepresentationItem.hxx>
#include <TColStd_HSequenceOfTransient.hxx>
#include <TransferBRep.hxx>
#include <Transfer_TransientProcess.hxx>
#include <UnitsMethods.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XSControl_TransferReader.hxx>
#include <XSControl_WorkSession.hxx>

#if OCC_VERSION_HEX >= 0x070800
#include <StepData_Factors.hxx>
#else
#include <StepData_GlobalFactors.hxx>
#endif

#if OCC_VERSION_HEX >= 0x070900
#include <XSAlgo_ShapeProcessor.hxx>
#else
#include <Resource_Manager.hxx>
#include <ShapeAlgo.hxx>
#include <ShapeExtend_MsgRegistrator.hxx>
#include <ShapeFix_Face.hxx>
#include <ShapeFix_Shape.hxx>
#include <ShapeFix_Solid.hxx>
#include <ShapeFix_Wire.hxx>
#include <ShapeProcess_ShapeContext.hxx>
#include <XSAlgo.hxx>
#include <XSAlgo_AlgoContainer.hxx>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <numeric>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cadly::cad::occt {
namespace {

using Clock = std::chrono::steady_clock;

void timing(ImportSummary& summary, const ImportOptions& options,
            const char* name, Clock::time_point start) {
  if (options.profile_timings) {
    summary.timings.push_back({name,
      std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start)});
  }
}

#if OCC_VERSION_HEX < 0x070900
// Older OCCT shares a cached Resource_Manager. The ordinary fallback healer
// only reads it, so construct each context under a lock, then perform healing
// on private shapes. Configured resource sequences write Runtime.Tolerance;
// those retain serial transfer. No Interface_Static setting is changed.
class LegacyStepProcessing final : public XSAlgo_AlgoContainer {
public:
  LegacyStepProcessing(const ImportOptions& options)
      : previous_(XSAlgo::AlgoContainer()), fast_(options.step_healing_mode ==
          StepHealingMode::Fast), profile_(options.profile_timings) {
    SetToolContainer(previous_->ToolContainer());
    ShapeAlgo::Init();
    const char* resource = Interface_Static::CVal("read.step.resource.name");
    const char* sequence = Interface_Static::CVal("read.step.sequence");
    resource_ = resource ? resource : "STEP";
    sequence_ = sequence ? sequence : "FromSTEP";
    ShapeProcess_ShapeContext context(resource_.c_str());
    resources_ = context.ResourceManager();
    configured_ = resources_->Find((sequence_ + ".exec.op").c_str());
    standard_provider_ = previous_->DynamicType() == STANDARD_TYPE(XSAlgo_AlgoContainer);
    // A custom sequence without this setting may not use FixShape at all.
    fast_supported_ = standard_provider_ && (!configured_ ||
      resources_->Find((sequence_ + ".FixShape.FixIntersectingEdgesMode").c_str()));
  }

  bool can_parallelize() const { return standard_provider_ && !configured_; }
  bool fast_supported() const { return fast_supported_; }
  void enable_parallel_contexts() { parallel_contexts_ = true; }

  void append_timings(ImportSummary& summary) const {
    if (!profile_) return;
    summary.timings.push_back({"STEP healing work (sum)",
      std::chrono::microseconds(healing_us_.load())});
  }

  using XSAlgo_AlgoContainer::ProcessShape;
  TopoDS_Shape ProcessShape(const TopoDS_Shape& shape, Standard_Real precision,
      Standard_Real max_tolerance, Standard_CString resource,
      Standard_CString sequence, Handle(Standard_Transient)& info,
      const Message_ProgressRange& range, Standard_Boolean non_manifold
#if OCC_VERSION_HEX >= 0x070700
      , TopAbs_ShapeEnum detail
#endif
      ) const override {
    if (shape.IsNull()) return shape;
    const auto start = Clock::now();
    TopoDS_Shape result;
    if (fast_ && fast_supported_ && !configured_) {
      Handle(ShapeProcess_ShapeContext) context = make_context(shape);
#if OCC_VERSION_HEX >= 0x070700
      context->SetDetalisation(detail);
#endif
      context->SetNonManifold(non_manifold);
      info = context;
      // Match XSAlgo's default STEP fallback, changing just the adjacent-edge
      // repair. In particular, keep the file precision and maximum tolerance.
      Handle(ShapeExtend_MsgRegistrator) messages = new ShapeExtend_MsgRegistrator;
      ShapeFix_Shape fixer;
      fixer.Init(shape);
      fixer.SetMsgRegistrator(messages);
      fixer.SetPrecision(precision);
      fixer.SetMaxTolerance(max_tolerance);
      fixer.FixFaceTool()->FixWireTool()->FixSameParameterMode() = false;
      fixer.FixSolidTool()->CreateOpenSolidMode() = false;
      fixer.FixWireTool()->FixIntersectingEdgesMode() = 0;
      try {
        fixer.Perform(range);
        result = fixer.Shape();
        if (!result.IsNull() && result != shape) {
          context->RecordModification(fixer.Context(), messages);
          context->SetResult(result);
        }
      } catch (const Standard_Failure&) {
        // Like the stock STEP fallback, keep the original shape if its
        // repair fails; translation may still provide usable display data.
      }
      result = context->Result();
    } else {
      if (parallel_contexts_) {
        auto context = make_context(shape);
#if OCC_VERSION_HEX >= 0x070700
        context->SetDetalisation(detail);
#endif
        info = context;
      }
      const std::string key = sequence_ + ".FixShape.FixIntersectingEdgesMode";
      const bool override_resource = fast_ && fast_supported_ && configured_;
      std::string previous_value;
      if (override_resource) {
        previous_value = resources_->Value(key.c_str());
        resources_->SetResource(key.c_str(), 0);
      }
      try {
        result = previous_->ProcessShape(shape, precision, max_tolerance,
          resource, sequence, info, range, non_manifold
#if OCC_VERSION_HEX >= 0x070700
          , detail
#endif
          );
      } catch (...) {
        if (override_resource) resources_->SetResource(key.c_str(), previous_value.c_str());
        throw;
      }
      if (override_resource) resources_->SetResource(key.c_str(), previous_value.c_str());
    }
    if (profile_) {
      healing_us_.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - start).count(), std::memory_order_relaxed);
    }
    return result;
  }

  void PrepareForTransfer() const override { previous_->PrepareForTransfer(); }
  Standard_Boolean CheckPCurve(const TopoDS_Edge& edge, const TopoDS_Face& face,
      Standard_Real precision, Standard_Boolean seam) const override {
    return previous_->CheckPCurve(edge, face, precision, seam);
  }
  void MergeTransferInfo(const Handle(Transfer_TransientProcess)& process,
      const Handle(Standard_Transient)& info, Standard_Integer first) const override {
    previous_->MergeTransferInfo(process, info, first);
  }
  void MergeTransferInfo(const Handle(Transfer_FinderProcess)& process,
      const Handle(Standard_Transient)& info) const override {
    previous_->MergeTransferInfo(process, info);
  }

private:
  Handle(ShapeProcess_ShapeContext) make_context(const TopoDS_Shape& shape) const {
    std::lock_guard<std::mutex> lock(context_mutex_);
    Handle(ShapeProcess_ShapeContext) context =
      new ShapeProcess_ShapeContext(shape, resource_.c_str());
    context->SetDetalisation(TopAbs_EDGE);
    return context;
  }

  Handle(XSAlgo_AlgoContainer) previous_;
  Handle(Resource_Manager) resources_;
  std::string resource_, sequence_;
  bool fast_, profile_;
  bool configured_{false}, standard_provider_{false}, fast_supported_{false};
  bool parallel_contexts_{false};
  mutable std::mutex context_mutex_;
  mutable std::atomic<std::int64_t> healing_us_{0};
};

class ProcessingLease {
public:
  explicit ProcessingLease(const Handle(LegacyStepProcessing)& provider)
      : previous_(XSAlgo::AlgoContainer()) {
    XSAlgo::SetAlgoContainer(provider);
  }
  ~ProcessingLease() { XSAlgo::SetAlgoContainer(previous_); }
private:
  Handle(XSAlgo_AlgoContainer) previous_;
};
#endif

struct BodyJob {
  Handle(Standard_Transient) source;
  Handle(StepShape_ShapeRepresentation) representation;
  Handle(STEPControl_ActorRead) actor;
  Handle(Transfer_TransientProcess) process;
#if OCC_VERSION_HEX >= 0x070800
  StepData_Factors factors;
#else
  std::array<double, 3> factors{{1.0, 1.0, 1.0}};
#endif
  bool succeeded{false};
};

// Curves/points may be shared as immutable input. Shared STEP faces, edges,
// wires or vertices require a common transfer map and must stay serial.
bool independent_topology(const std::vector<BodyJob>& jobs,
                          const Interface_Graph& graph, IProgressSink& progress) {
  std::unordered_map<const void*, std::size_t> owners;
  std::vector<Handle(Standard_Transient)> pending;
  std::size_t visited = 0;
  for (std::size_t owner = 0; owner < jobs.size(); ++owner) {
    pending.push_back(jobs[owner].source);
    while (!pending.empty()) {
      if ((visited++ % 256) == 0 && progress.cancelled()) return false;
      auto entity = pending.back();
      pending.pop_back();
      auto children = graph.Shareds(entity);
      for (; children.More(); children.Next()) {
        const auto child = children.Value();
        if (!child->IsKind(STANDARD_TYPE(StepShape_TopologicalRepresentationItem))) continue;
        const auto entry = owners.emplace(child.get(), owner);
        if (!entry.second) {
          if (entry.first->second != owner) return false;
        } else {
          pending.push_back(child);
        }
      }
    }
  }
  return true;
}

std::vector<BodyJob> collect_jobs(STEPControl_Reader& reader,
                                 IProgressSink& progress) {
  std::vector<BodyJob> jobs;
  std::unordered_map<const void*, const void*> contexts;
  const auto model = reader.StepModel();
  for (Standard_Integer i = 1; i <= model->NbEntities(); ++i) {
    if ((i % 1024) == 0 && progress.cancelled()) return {};
    const auto representation = Handle(StepShape_ShapeRepresentation)::DownCast(model->Value(i));
    if (representation.IsNull() ||
        representation->IsKind(STANDARD_TYPE(StepShape_NonManifoldSurfaceShapeRepresentation))) continue;
    for (Standard_Integer j = 1; j <= representation->NbItems(); ++j) {
      const auto item = representation->ItemsValue(j);
      if (item.IsNull() || !item->IsKind(STANDARD_TYPE(StepShape_ManifoldSolidBrep))) continue;
      const auto context = representation->ContextOfItems();
      const auto entry = contexts.emplace(item.get(), context.get());
      if (!entry.second) {
        // The same body in different contexts can have different units.
        // Leave that resolution to the ordinary reader and its transfer map.
        if (entry.first->second != context.get()) return {};
        continue;
      }
      BodyJob job;
      job.source = item;
      job.representation = representation;
      jobs.push_back(std::move(job));
    }
  }
  return jobs;
}

bool prepare_jobs(std::vector<BodyJob>& jobs, STEPControl_Reader& reader,
                  double unit_mm, IProgressSink& progress) {
  const auto model = reader.StepModel();
  for (auto& job : jobs) {
    if (progress.cancelled()) return false;
    job.process = new Transfer_TransientProcess(1000);
    job.process->SetGraph(reader.WS()->HGraph());
#if OCC_VERSION_HEX >= 0x070800
    job.actor = new STEPControl_ActorRead(model);
    job.factors.SetCascadeUnit(unit_mm);
    job.actor->PrepareUnits(job.representation, job.process, job.factors);
#else
    (void)unit_mm;
    job.actor = new STEPControl_ActorRead;
    job.actor->PrepareUnits(job.representation, job.process);
    auto& factors = StepData_GlobalFactors::Intance();
    job.factors = {{factors.LengthFactor(), factors.PlaneAngleFactor(), factors.SolidAngleFactor()}};
    for (const double factor : job.factors)
      if (!std::isfinite(factor) || factor <= 0.0) return false;
#endif
#if OCC_VERSION_HEX >= 0x070900
    job.actor->SetShapeFixParameters(reader.GetShapeFixParameters());
    job.actor->SetProcessingFlags(reader.GetShapeProcessFlags().first);
#endif
  }
  return true;
}

unsigned run_jobs(std::vector<BodyJob>& jobs, unsigned worker_count,
                  IProgressSink& progress) {
  Handle(OcctProgressBridge) bridge = new OcctProgressBridge(
    progress, 0.28f, 0.56f, "Converting STEP bodies...");
  Message_ProgressScope scope(bridge->Start(), "Bodies", static_cast<double>(jobs.size()));
  // Unconsumed ranges still refer to their parent scope on destruction.
  // Keep them here so cancellation destroys them before the scope closes.
  std::vector<Message_ProgressRange> ranges;
  ranges.reserve(jobs.size());
  for (std::size_t i = 0; i < jobs.size(); ++i) ranges.push_back(scope.Next());
  std::vector<std::size_t> order(jobs.size());
  std::iota(order.begin(), order.end(), 0);
#if OCC_VERSION_HEX < 0x070800
  // Legacy OCCT shares conversion factors globally. Complete a group before
  // changing them; each actor still owns its representation's precision.
  std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    return jobs[a].factors < jobs[b].factors;
  });
#endif
  unsigned actual_workers = 1;
  for (std::size_t begin = 0; begin < order.size() && !progress.cancelled();) {
    std::size_t end = order.size();
#if OCC_VERSION_HEX < 0x070800
    const auto& factors = jobs[order[begin]].factors;
    end = begin + 1;
    while (end < order.size() && jobs[order[end]].factors == factors) ++end;
    StepData_GlobalFactors::Intance().InitializeFactors(factors[0], factors[1], factors[2]);
#endif
    std::atomic<std::size_t> next{begin};
    auto work = [&]() {
      while (!progress.cancelled()) {
        const auto position = next.fetch_add(1, std::memory_order_relaxed);
        if (position >= end) return;
        const auto index = order[position];
        auto& job = jobs[index];
        try {
          const auto binder = job.actor->TransferShape(job.source, job.process,
#if OCC_VERSION_HEX >= 0x070800
            job.factors,
#endif
            true, false, ranges[index]);
          job.succeeded = !TransferBRep::ShapeResult(binder).IsNull();
        } catch (...) {
          // The ordinary reader retries this body with its normal error path.
          job.succeeded = false;
        }
      }
    };
    const auto group_workers = static_cast<unsigned>(std::min<std::size_t>(worker_count, end - begin));
    std::vector<std::thread> threads;
    threads.reserve(group_workers - 1);
    struct Join {
      std::vector<std::thread>& threads;
      ~Join() { for (auto& thread : threads) if (thread.joinable()) thread.join(); }
    } join{threads};
    try {
      for (unsigned i = 1; i < group_workers; ++i) threads.emplace_back(work);
    } catch (const std::system_error&) {
      // Finish with the threads the host was able to create.
    }
    work();
    for (auto& thread : threads) thread.join();
    actual_workers = std::max(actual_workers, static_cast<unsigned>(threads.size()) + 1);
    begin = end;
  }
  return actual_workers;
}

} // namespace

bool transfer_step(STEPCAFControl_Reader& reader,
                   Handle(TDocStd_Document)& document,
                   const ImportOptions& options, ImportSummary& summary,
                   IProgressSink& progress) {
  auto& bare = reader.ChangeReader();
  const auto model = bare.StepModel();
  summary.step_entity_count = static_cast<std::size_t>(model->NbEntities());
  summary.step_healing_mode = options.step_healing_mode;
  bool allow_parallel = options.parallel_step_transfer && options.step_transfer_threads != 1;

#if OCC_VERSION_HEX >= 0x070900
  // An empty transfer initializes the reader's own version-specific defaults
  // and processing flags without translating geometry.
  bare.TransferList(new TColStd_HSequenceOfTransient);
  if (options.step_healing_mode == StepHealingMode::Fast) {
    auto parameters = bare.GetShapeFixParameters();
    parameters.Bind("FixShape.FixIntersectingEdgesMode", "0");
    bare.SetShapeFixParameters(std::move(parameters));
  }
  if (model->InternalParameters.ReadNonmanifold) allow_parallel = false;
#else
  Handle(LegacyStepProcessing) processing = new LegacyStepProcessing(options);
  ProcessingLease processing_lease(processing);
  allow_parallel = allow_parallel && processing->can_parallelize() &&
    Interface_Static::IVal("read.step.nonmanifold") == 0;
  if (options.step_healing_mode == StepHealingMode::Fast && !processing->fast_supported()) {
    summary.step_healing_mode = StepHealingMode::Full;
    summary.diagnostics.push_back({DiagnosticSeverity::Info,
      "The custom OCCT shape-processing configuration requires full STEP repair."});
  }
#endif

  auto start = Clock::now();
  std::vector<BodyJob> jobs;
  unsigned workers = std::min(64u, options.step_transfer_threads);
  if (workers == 0) workers = std::min(4u, std::max(1u, std::thread::hardware_concurrency()));
  try {
    if (allow_parallel) jobs = collect_jobs(bare, progress);
  } catch (const Standard_Failure&) {
    // Malformed representation graphs retain OCCT's normal recovery path.
    jobs.clear();
  }
  workers = static_cast<unsigned>(std::min<std::size_t>(workers, jobs.size()));
  try {
    if (workers > 1 && !independent_topology(jobs, bare.WS()->Graph(), progress)) {
      workers = 1;
      if (options.profile_timings) summary.diagnostics.push_back({DiagnosticSeverity::Info,
        "STEP bodies share topology; using the ordinary transfer map."});
    }
  } catch (const Standard_Failure&) {
    workers = 1;
  }

  if (workers > 1) {
    // Match STEPCAFControl_Reader::prepareUnits before pre-transferring bodies.
    double unit_mm = 1.0;
    if (!XCAFDoc_DocumentTool::GetLengthUnit(document, unit_mm, UnitsMethods_LengthUnit_Millimeter)) {
#if OCC_VERSION_HEX >= 0x070900
      XSAlgo_ShapeProcessor::PrepareForTransfer();
#else
      XSAlgo::AlgoContainer()->PrepareForTransfer();
#endif
      unit_mm = UnitsMethods::GetCasCadeLengthUnit();
      XCAFDoc_DocumentTool::SetLengthUnit(document, unit_mm, UnitsMethods_LengthUnit_Millimeter);
    }
    model->SetLocalLengthUnit(unit_mm);
    bool prepared = false;
    try {
      prepared = prepare_jobs(jobs, bare, unit_mm, progress);
    } catch (const Standard_Failure&) {
      // A broken unit context must not bypass the reader's diagnostics.
    }
    if (!prepared) {
      workers = 1;
      if (options.profile_timings) summary.diagnostics.push_back({DiagnosticSeverity::Info,
        "STEP unit contexts could not be prepared; using the ordinary transfer path."});
    }
  }
  timing(summary, options, "STEP body preparation", start);
  if (progress.cancelled()) return false;
  if (workers <= 1) jobs.clear();

  bool pretransferred = false;
  if (workers > 1) {
#if OCC_VERSION_HEX < 0x070900
    processing->enable_parallel_contexts();
#endif
    summary.step_body_count = jobs.size();
    start = Clock::now();
    summary.step_transfer_threads = run_jobs(jobs, workers, progress);
    timing(summary, options, "STEP independent body transfer", start);
    if (progress.cancelled()) return false;

    start = Clock::now();
    auto transfer_reader = bare.WS()->TransferReader();
    transfer_reader->BeginTransfer();
    auto master = transfer_reader->TransientProcess();
    master->SetGraph(bare.WS()->HGraph());
    for (const auto& job : jobs) {
      if (!job.succeeded) continue;
      for (Standard_Integer i = 1; i <= job.process->NbMapped(); ++i) {
        const auto entity = job.process->Mapped(i);
        if (!master->IsBound(entity)) master->Bind(entity, job.process->MapItem(i));
      }
    }
    jobs.clear();
    timing(summary, options, "STEP transfer map merge", start);
    pretransferred = true;
  }

  // STEPCAF builds assemblies and metadata using the completed body binders.
  // Unsupported/failed bodies still go through OCCT's ordinary transfer.
  Handle(OcctProgressBridge) bridge = new OcctProgressBridge(progress,
    pretransferred ? 0.56f : 0.25f, 0.60f, "Building STEP assembly...");
  start = Clock::now();
  const bool transferred = reader.Transfer(document, bridge->Start());
  timing(summary, options, pretransferred ? "STEP XCAF assembly/remainder" :
    "STEP serial transfer", start);
#if OCC_VERSION_HEX < 0x070900
  processing->append_timings(summary);
#endif
  if (summary.step_healing_mode == StepHealingMode::Fast) {
    summary.diagnostics.push_back({DiagnosticSeverity::Info,
      "Fast STEP viewing skips adjacent-edge intersection repair; use full repair for damaged geometry."});
  }
  return transferred;
}

} // namespace cadly::cad::occt
