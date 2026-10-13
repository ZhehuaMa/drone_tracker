from pathlib import Path
import re
root=Path(__file__).parent/'source'
p=root/'src/diagnostics/recorder.h';s=p.read_text().replace('inline void (*fixed_hook)', 'inline void (*fixed_preparation_hook)(std::uint64_t, Type) noexcept{};\ninline std::uint64_t preparationStart() noexcept { return fixed_preparation_hook ? monotonicNs() : 0; }\ninline void preparationEnd(std::uint64_t start, Type type) noexcept { if (fixed_preparation_hook) { fixed_preparation_hook(monotonicNs() - start, type); } }\ninline void (*fixed_hook)');p.write_text(s)
p=root/'src/hybrid_tracker/hybrid_tracker.cpp';s=p.read_text()
for name in ['value','initialization','step','summary','entered','added','removed','lookup','basis','timeout']:
 # 一次局部事件创建到首次提交之间不包含实际模型推理；仅测指定局部字段准备。
 pattern=r'auto '+name+r'\{diagnostics::event\('
 s=re.sub(pattern, 'const auto preparation_'+name+'{diagnostics::preparationStart()};\n    auto '+name+'{diagnostics::event(', s)
 # 所有有此名字的第一提交位置（initialization第二次复用另行处理）
 if name=='initialization':
  s=s.replace('diagnostics::emit(initialization);','diagnostics::preparationEnd(preparation_initialization, initialization.type); diagnostics::emit(initialization);',1)
 elif name=='value':
  s=s.replace('diagnostics::emit(value); value.type', 'diagnostics::preparationEnd(preparation_value, value.type); diagnostics::emit(value); value.type',1)
 else:
  s=s.replace('diagnostics::emit('+name+');','diagnostics::preparationEnd(preparation_'+name+', '+name+'.type); diagnostics::emit('+name+');')
s=s.replace('initialization.type = diagnostics::Type::InitializeEnd;', 'const auto preparation_initialization_end{diagnostics::preparationStart()};\n    initialization.type = diagnostics::Type::InitializeEnd;')
s=s.replace('diagnostics::emit(initialization);\n    if (!initialized)', 'diagnostics::preparationEnd(preparation_initialization_end, initialization.type); diagnostics::emit(initialization);\n    if (!initialized)');p.write_text(s)
p=root/'src/fixed_workload_probe.cpp';s=p.read_text().replace('std::uint32_t events{}, init{}, steps{};', 'std::uint32_t events{}, init{}, steps{}, preparation_count{};\n    std::uint64_t preparation_ns{};')
s=s.replace('class Observer final', 'void preparationHook(std::uint64_t ns, diagnostics::Type) noexcept { if (sample) { sample->preparation_ns += ns; ++sample->preparation_count; } }\nclass Observer final')
s=s.replace('diagnostics::fixed_hook = observed || audit ? hook : nullptr;', 'diagnostics::fixed_hook = observed || audit ? hook : nullptr;\n        diagnostics::fixed_preparation_hook = observed ? preparationHook : nullptr;')
s=s.replace('diagnostics::fixed_hook = nullptr; tracker->finishRound();', 'diagnostics::fixed_hook = nullptr; diagnostics::fixed_preparation_hook = nullptr; tracker->finishRound();')
s=s.replace('rvalid";', 'rvalid,preparation_ns,preparation_count";').replace("<< r.is_valid;", "<< r.is_valid << ',' << v.preparation_ns << ',' << v.preparation_count;")
s=s.replace('observer_span_ns\\n','observer_span_ns,event_hook_ns,preparation_empty_ns\\n')
s=s.replace("<< observer_cost[i] << '\\n';", "<< observer_cost[i] << ','; Sample calibration_sample{}; sample = &calibration_sample; auto hook_start{diagnostics::monotonicNs()}; hook(diagnostics::event(diagnostics::Type::UpdateEnd)); calibration << diagnostics::monotonicNs() - hook_start << ','; hook_start = diagnostics::monotonicNs(); auto preparation_start{diagnostics::monotonicNs()}; preparationHook(diagnostics::monotonicNs() - preparation_start, diagnostics::Type::UpdateEnd); calibration << diagnostics::monotonicNs() - hook_start << '\\n'; sample = nullptr;")
p.write_text(s)
