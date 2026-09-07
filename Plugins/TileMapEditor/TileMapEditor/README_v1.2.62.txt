TileMapEditor UE4.27 v1.2.62 - Completed Mesh Edge Diagnostics

Baseline
--------
This diagnostic revision is based on v1.2.60, whose terrain geometry is the
accepted v1.2.55 implementation plus instrumentation. It does not include the
rejected v1.2.61 support-cap experiment.

Purpose
-------
The existing Debug Slant Supports option now also analyzes the completed
procedural-mesh sections after every source polygon has been generated. It
groups edges by quantized geometric endpoints instead of vertex indices, then
marks horizontal edges near stacked horizontal slants whose final incidence is
not exactly two.

Visualization
-------------
Red: one-sided/unmatched completed-mesh edge. This is a true open boundary or
one side of a cross-piece T-junction.

Orange: completed-mesh edge used by more than two triangles (non-manifold).

Yellow/green/cyan: the existing v1.2.60 per-support-polygon diagnostics.

Output Log
----------
Filter for:

TileMap completed edge diagnostic

The per-edge records contain endpoints, midpoint, length, incidence count and
first material tile type. Each affected chunk also emits a summary.

Scope
-----
This revision does not add, remove, move or retriangulate terrain vertices. It
does not change masks, UVs, normals, materials, collision, baking or topology.
The diagnostic runs only when Debug Slant Supports is enabled and does not run
for the modular path overlay.
