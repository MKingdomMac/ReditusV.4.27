Pixel2D runtime Animation Blueprint setter for UE 4.27
======================================================

Change included
---------------
UPixel2DComponent now exposes this Blueprint-callable node:

    Set Pixel2D Anim Instance Class

Its input accepts a Pixel2D Anim Instance Class Reference. The implementation
uses the plugin's existing SetAnimInstance code, so changing the class also
reinitializes the Pixel2D animation instance.

Installation
------------
1. Close Unreal Editor and Visual Studio.
2. Remove or move aside the old Pixel2D plugin folder. Do not merge the two
   plugin folders.
3. Copy the Pixel2D folder supplied by this package into your project's Plugins
   folder.
4. Regenerate Visual Studio project files from the .uproject file.
5. Build the Editor target in Development Editor / Win64.
6. Open Unreal Editor and compile BP_UnitBase before using the new node.

The package intentionally omits the old precompiled Binaries directory so the
plugin is rebuilt with the new Blueprint function.

Blueprint use
-------------
Drag from the Pixel2D component and search for:

    Set Pixel2D Anim Instance Class

Call it after resolving the unit's DataTable row. The class pin should be a
Pixel2D Anim Instance Class Reference, not a general Actor class.
