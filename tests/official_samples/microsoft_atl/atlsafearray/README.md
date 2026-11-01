# Original Microsoft ArrayManager source subset

Unmodified files from microsoft/VCSamples@9e1d4475555b76a17a3568369867f1d7b6cc6126,
VC2010Samples/ATL/General/atlsafearray. The sibling commap/original_sources.json
records exact Git blobs for these five files and the existing canonical
../../../atl_rgs/fixtures/ArrayManager.rgs (relative to commap). The same byte
checker/configure graph verifies all 36 original blobs. The upstream MIT license
is retained in ../MICROSOFT_LICENSE.txt.

Native Windows SDK MIDL generates the actual header/IIDs/TypeLib. The unchanged
class is compiled into the existing STA ActiveX events binary against
OpenWTL::WTL. Its harness uses actual factories/CoCreateInstance, the original
RGS callback in an isolated root, IObjectSafety and per-user registered dispatch.
Add/Change/Remove/GetArray results own genuine independent VARIANT SAFEARRAYs.
Snapshots survive object/module retirement. No class method, macro, IID, IDL or
RGS is replaced; no target, test mode or workflow is added. Original methods'
ignored HRESULTs are preserved; valid-input behavior is admitted separately
from wrapper failure handling. Original DLL entrypoints/full .rc/.def/project
deployment, internal ATL type-wrapper aliases and exact binary layouts remain
unqualified. See docs/ATL_COMPATIBILITY.md and docs/OFFICIAL_SAMPLES.md.
