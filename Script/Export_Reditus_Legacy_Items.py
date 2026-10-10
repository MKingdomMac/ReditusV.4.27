"""
Reditus legacy item Class Defaults exporter for Unreal Engine 4.27.

This is an editor-only data extraction script. It does not modify assets,
gameplay Blueprints, save data, or packaged-game code.

Requirements:
  - Enable the Python Editor Script Plugin.
  - Enable Editor Scripting Utilities if EditorAssetLibrary is unavailable.

Run from Unreal Editor's Output Log:
  py "C:/Full/Path/Export_Reditus_Legacy_Items.py"

Output:
  <Project>/Saved/ItemExports/Reditus_LegacyItems_ClassDefaults.csv
"""

import csv
import os
import re

import unreal


ITEM_ROOT = "/Game/TurnBasedJRPGTemplate/Blueprints/Items"
OUTPUT_DIRECTORY = os.path.join(unreal.Paths.project_saved_dir(), "ItemExports")
OUTPUT_FILE = os.path.join(OUTPUT_DIRECTORY, "Reditus_LegacyItems_ClassDefaults.csv")


# Framework classes and helper actors are not inventory records.
SKIP_ASSET_NAMES = {
    "BP_ItemBase",
    "BP_RecipeItemBase",
    "BP_CraftItemBase",
    "BP_EquipmentBase",
    "BP_AccessoriesBase",
    "BP_AccessoriesBase_Child",
    "BP_ArmorBase",
    "BP_BootsBase",
    "BP_HelmBase",
    "BP_ShieldBase",
    "BP_WeaponBase",
    "BP_UsableItemBase",
    "BP_SpecializedItem",
    "BP_RobotCoreBase",
    "BP_MiscItemBase",
}


# These graph-use flags come from the supplied Reditus project dump. They are
# included so the export records how each legacy usable item is currently used.
BATTLE_USE_CLASSES = {
    "BP_AdvancedMedkit",
    "BP_Antidote",
    "BP_BluePotion",
    "BP_EchoScreen",
    "BP_Ether",
    "BP_KwehMeat",
    "BP_Maal",
    "BP_Medkit",
    "BP_PheonixDown",
    "BP_RedPotion",
    "Bp_Decimator",
    "Bp_EmpGrenades",
    "Bp_Shuriken",
}


OUTSIDE_BATTLE_USE_CLASSES = {
    "BP_AdvancedMedkit",
    "BP_Antidote",
    "BP_BluePotion",
    "BP_EchoScreen",
    "BP_Ether",
    "BP_FecalMatter",
    "BP_KwehMeat",
    "BP_Maal",
    "BP_Medkit",
    "BP_PheonixDown",
    "BP_RedPotion",
    "BP_WaypointItem",
    "Bp_Tent",
}


BEHAVIOR_SUMMARIES = {
    "BP_AdvancedMedkit": "Heal 100 HP in battle; legacy graph comment says 30% HP",
    "BP_Antidote": "Remove Poison in and outside battle",
    "BP_BluePotion": "Restore 25% MaxMP in and outside battle",
    "BP_EchoScreen": "Remove Blind in and outside battle",
    "BP_Ether": "Increase MP by 50 in battle; legacy comment says 150; outside graph says 25% MaxMP",
    "BP_FecalMatter": "Special outside-battle story action: Pick Rose with Shit",
    "BP_KwehMeat": "Restore 100% MaxHP in and outside battle",
    "BP_Maal": "Remove Berserk in and outside battle",
    "BP_Medkit": "Heal 50 HP in battle; legacy graph comment says 30% HP",
    "BP_PheonixDown": "Revive unit in and outside battle",
    "BP_RedPotion": "Restore 15% MaxHP in and outside battle",
    "BP_WaypointItem": "Spawn/drop waypoint device outside battle",
    "Bp_Decimator": "Deal 30 pure damage after EMP grenade projectile hit",
    "Bp_EmpGrenades": "Deal 30 pure damage after EMP grenade projectile hit",
    "Bp_Shuriken": "Deal 30 pure damage after flesh-bullet projectile hit",
    "Bp_Tent": "Restore party runtime resources outside battle",
}


PROPERTY_CANDIDATES = {
    "DisplayName": ["name"],
    "ShortDescription": ["shortDescription", "short_description"],
    "LongDescription": ["longDescription", "long_description"],
    "Price": ["price"],
    "ThumbnailRaw": ["thumbnail"],
    "UnitFilter": ["unitFilter", "unit_filter"],
    "LegacyElement": ["Element", "element"],
    "LegacyDamageType": ["damageType", "damage_type"],
    "LegacySkillType": ["Skilltype", "skilltype", "skill_type"],
    "AlwaysHit": ["AlwaysHit", "always_hit"],
    "NameForStealing": ["NameForStealing", "name_for_stealing"],
    "OnlyExploreMode": ["OnlyExploreMode", "only_explore_mode"],
    "LegacyBuffs": ["buffs"],
    "StatsRaw": ["stats"],
    "BattleSkills": ["battleSkills", "battle_skills"],
    "ExploreSkills": ["exploreSkills", "explore_skills"],
    "EquipmentType": ["equipmentType", "equipment_type"],
    "RequiredClasses": ["requiredClasses", "required_classes"],
    "EquipmentMesh": ["equipmentMesh", "equipment_mesh"],
    "Immunities": ["Immunities", "immunities"],
    "OverdriveTechs": ["OverdriveTechs", "overdrive_techs"],
    "UpgradeMaterials": ["UpgradeMaterials", "upgrade_materials"],
    "UpgradeItem": ["UpgradeItem", "upgrade_item"],
    "CanBeUpgraded": ["Can Be Upgraded?", "can_be_upgraded", "CanBeUpgraded"],
    "SuccessChance": ["successChance", "success_chance"],
    "CraftedItem": ["craftedItem", "crafted_item"],
    "Recipe": ["recipe"],
}


THUMBNAIL_TEXTURE_FIELDS = [
    "thumbnail",
    "thumbnail_2_65687B0D4850E998159EC2A8AC913A92",
]


THUMBNAIL_SPRITE_FIELDS = [
    "Spritethumbnail",
    "sprite_thumbnail",
    "Spritethumbnail_5_77B20F4249EA160DF53539ABF74756FC",
]


EXPORT_COLUMNS = [
    "ItemID",
    "LegacyClass",
    "LegacyAssetPath",
    "Category",
    "LegacyFamily",
    "CanUseInBattleFromDump",
    "CanUseOutsideBattleFromDump",
    "LegacyBehaviorSummary",
    "DisplayName",
    "ShortDescription",
    "LongDescription",
    "Price",
    "ThumbnailTexture",
    "ThumbnailSprite",
    "ThumbnailRaw",
    "UnitFilter",
    "LegacyElement",
    "LegacyDamageType",
    "LegacySkillType",
    "AlwaysHit",
    "NameForStealing",
    "OnlyExploreMode",
    "LegacyBuffs",
    "StatsRaw",
    "BattleSkills",
    "ExploreSkills",
    "EquipmentType",
    "RequiredClasses",
    "EquipmentMesh",
    "Immunities",
    "OverdriveTechs",
    "UpgradeMaterials",
    "UpgradeItem",
    "CanBeUpgraded",
    "SuccessChance",
    "CraftedItem",
    "Recipe",
    "Warnings",
]


_MISSING = object()


def log(message):
    unreal.log("[Reditus Item Export] {}".format(message))


def warn(message):
    unreal.log_warning("[Reditus Item Export] {}".format(message))


def asset_name_from_path(asset_path):
    object_name = asset_path.rsplit("/", 1)[-1]
    return object_name.split(".", 1)[0]


def item_id_from_class_name(class_name):
    value = re.sub(r"^(BP|Bp)_", "", class_name)
    value = value.replace("+", "Plus")
    value = re.sub(r"[^A-Za-z0-9_]+", "_", value)
    return re.sub(r"_+", "_", value).strip("_")


def category_from_path(asset_path):
    if "/Equipment/" in asset_path:
        return "Equipment"
    if "/UsableItems/" in asset_path:
        return "Consumable"
    if "/CraftItems/" in asset_path:
        return "Recipe"
    if "/KeyItems/" in asset_path:
        return "KeyItem"
    if "/MiscItems/" in asset_path:
        return "Misc"
    return "Misc"


def family_from_path(asset_path):
    families = [
        ("/UpgradeWeapons/", "UpgradeWeapon"),
        ("/Weapons/", "Weapon"),
        ("/Armors/", "Armor"),
        ("/Boots/", "Boots"),
        ("/Helms/", "Helm"),
        ("/Shields/", "Shield"),
        ("/Accesories/", "Accessory"),
        ("/RobotCores/", "RobotCore"),
        ("/WeaponItems/", "BattleItem"),
        ("/UsableItems/", "UsableItem"),
        ("/CraftItems/", "Recipe"),
        ("/KeyItems/", "KeyItem"),
        ("/MiscItems/", "MiscItem"),
    ]
    for marker, family in families:
        if marker in asset_path:
            return family
    return "Item"


def read_editor_property(value, candidates):
    if value is None:
        return _MISSING
    for candidate in candidates:
        try:
            return value.get_editor_property(candidate)
        except Exception:
            pass
        try:
            return getattr(value, candidate)
        except Exception:
            pass
    return _MISSING


def object_path(value):
    if value is None:
        return "None"
    for method_name in ("get_path_name", "get_full_name"):
        try:
            method = getattr(value, method_name)
            return str(method())
        except Exception:
            pass
    return ""


def value_to_text(value):
    if value is _MISSING or value is None:
        return ""
    if isinstance(value, bool):
        return "True" if value else "False"
    if isinstance(value, (int, float, str)):
        return str(value)

    path = object_path(value)
    if path:
        return path

    try:
        if hasattr(value, "items"):
            pairs = []
            for key, item_value in value.items():
                pairs.append("{}={}".format(value_to_text(key), value_to_text(item_value)))
            return "(" + ",".join(pairs) + ")"
    except Exception:
        pass

    try:
        if not isinstance(value, (bytes, bytearray)):
            return "(" + ",".join(value_to_text(item) for item in value) + ")"
    except Exception:
        pass

    try:
        return str(value)
    except Exception:
        return repr(value)


def load_blueprint_class(asset_path):
    try:
        generated_class = unreal.EditorAssetLibrary.load_blueprint_class(asset_path)
        if generated_class:
            return generated_class
    except Exception:
        pass

    try:
        blueprint = unreal.EditorAssetLibrary.load_asset(asset_path)
        if blueprint:
            generated_class = blueprint.generated_class()
            if generated_class:
                return generated_class
    except Exception:
        pass

    return None


def default_object_for_class(generated_class):
    try:
        return unreal.get_default_object(generated_class)
    except Exception:
        pass
    try:
        return generated_class.get_default_object()
    except Exception:
        return None


def export_row(asset_path):
    class_name = asset_name_from_path(asset_path)
    warnings = []
    row = {column: "" for column in EXPORT_COLUMNS}
    row.update({
        "ItemID": item_id_from_class_name(class_name),
        "LegacyClass": class_name,
        "LegacyAssetPath": asset_path,
        "Category": category_from_path(asset_path),
        "LegacyFamily": family_from_path(asset_path),
        "CanUseInBattleFromDump": "True" if class_name in BATTLE_USE_CLASSES else "",
        "CanUseOutsideBattleFromDump": "True" if class_name in OUTSIDE_BATTLE_USE_CLASSES else "",
        "LegacyBehaviorSummary": BEHAVIOR_SUMMARIES.get(class_name, ""),
    })

    generated_class = load_blueprint_class(asset_path)
    if not generated_class:
        warnings.append("Could not load generated Blueprint class")
        row["Warnings"] = "; ".join(warnings)
        return row

    default_object = default_object_for_class(generated_class)
    if not default_object:
        warnings.append("Could not load Class Default Object")
        row["Warnings"] = "; ".join(warnings)
        return row

    raw_values = {}
    for output_name, candidates in PROPERTY_CANDIDATES.items():
        raw_value = read_editor_property(default_object, candidates)
        raw_values[output_name] = raw_value
        row[output_name] = value_to_text(raw_value)

    thumbnail_value = raw_values.get("ThumbnailRaw", _MISSING)
    if thumbnail_value is not _MISSING:
        row["ThumbnailTexture"] = value_to_text(
            read_editor_property(thumbnail_value, THUMBNAIL_TEXTURE_FIELDS)
        )
        row["ThumbnailSprite"] = value_to_text(
            read_editor_property(thumbnail_value, THUMBNAIL_SPRITE_FIELDS)
        )

    if not row["DisplayName"]:
        warnings.append("DisplayName is blank or could not be read")
    if row["Price"] == "":
        warnings.append("Price could not be read")
    if not row["ThumbnailTexture"] and not row["ThumbnailSprite"]:
        warnings.append("Thumbnail fields are blank or could not be read")

    row["Warnings"] = "; ".join(warnings)
    return row


def main():
    os.makedirs(OUTPUT_DIRECTORY, exist_ok=True)

    asset_paths = unreal.EditorAssetLibrary.list_assets(
        ITEM_ROOT,
        recursive=True,
        include_folder=False,
    )

    item_assets = []
    for asset_path in asset_paths:
        asset_path = str(asset_path)
        class_name = asset_name_from_path(asset_path)
        if class_name in SKIP_ASSET_NAMES:
            continue
        if "/EquipmentMesh/" in asset_path:
            continue
        item_assets.append(asset_path)

    item_assets.sort(key=lambda path: (category_from_path(path), item_id_from_class_name(asset_name_from_path(path)).lower()))
    rows = [export_row(asset_path) for asset_path in item_assets]

    with open(OUTPUT_FILE, "w", newline="", encoding="utf-8") as output_handle:
        writer = csv.DictWriter(output_handle, fieldnames=EXPORT_COLUMNS, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)

    warning_count = sum(1 for row in rows if row["Warnings"])
    log("Exported {} item Class Defaults".format(len(rows)))
    log("Rows with warnings: {}".format(warning_count))
    log("CSV written to: {}".format(os.path.normpath(OUTPUT_FILE)))

    if not rows:
        warn("No item Blueprints were found under {}".format(ITEM_ROOT))


if __name__ == "__main__":
    main()
