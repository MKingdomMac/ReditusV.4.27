"""Export Reditus equipment Blueprint Class Defaults to a DT_Equipment CSV.

Unreal Engine 4.27.2; Python Editor Script Plugin + Editor Scripting Utilities.
Run inside Unreal's Output Log (replace the path with where you saved this file):
    py "C:/Full/Path/Export_Reditus_Equipment_DT.py"

Output: <Project>/Saved/EquipmentExports/DT_Equipment_Import.csv
Excel: <Project>/Saved/EquipmentExports/DT_Equipment_Review.csv
Report: <Project>/Saved/EquipmentExports/DT_Equipment_Export_Report.txt

Uses the 17 fields in the supplied S_EquipmentDefintions screenshot. Reads
resolved class defaults, including inherited defaults and child overrides.
Does not spawn actors, execute graphs, modify Blueprints, or import a table.
Existing DT_Equipment rows are retained and matching rows are updated from BPs.
Newer fields missing from the old Blueprints keep their current table value;
otherwise the documented defaults below are used and listed in the report.
The misleadingly named 'Required Classes' column is S_UnitStats in this project,
and is kept separate from the 'Required Class' E_UnitClass array.

CSV nested values use Unreal struct/array/map text, NOT Python repr or JSON.
The Review CSV also gives each of the 12 equipment stats a numeric column and
lists only successfully read Blueprint classes, with their source class path.
Use Import.csv for the DataTable and Review.csv to inspect numbers in Excel.
Only the exported CSV is intended for DataTable import. If any Blueprint read
fails, no new CSV is written. The exact errors appear in the Output Log and
report. A previous CSV from this script is moved aside to prevent confusion.

UE4.27 API references:
https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/module/unreal?application_version=4.27
https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/EditorAssetLibrary?application_version=4.27
https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/DataTableFunctionLibrary?application_version=4.27
"""

import csv
import datetime
import json
import math
import os
import re

import unreal


# Project-specific paths confirmed in the Reditus project dump.
ITEM_ROOT = "/Game/TurnBasedJRPGTemplate/Blueprints/Items"
TABLE_ROOT = "/Game/TurnBasedJRPGTemplate/Blueprints/DataTable"
EQUIPMENT_TABLE = TABLE_ROOT + "/DT_Equipment"
EQUIPMENT_ROOT = ITEM_ROOT + "/Equipment"
BASE_PATHS = {
    "Weapon": EQUIPMENT_ROOT + "/Weapons/BP_WeaponBase",
    "Armor": EQUIPMENT_ROOT + "/Armors/BP_ArmorBase",
    "Helm": EQUIPMENT_ROOT + "/Helms/BP_HelmBase",
    "Boot": EQUIPMENT_ROOT + "/Boots/BP_BootsBase",
    "Accessory": EQUIPMENT_ROOT + "/Accesories/BP_AccessoriesBase",
}
SKIP_NAMES = {
    "bp_equipmentbase", "bp_weaponbase", "bp_armorbase", "bp_helmbase",
    "bp_bootsbase", "bp_accessoriesbase", "bp_shieldbase",
}

# Optional explicit exceptions: keys are full Blueprint/class paths or BP names.
# Normally BP_LeatherArmor -> LeatherArmor, matching the existing table IDs.
ROW_ID_OVERRIDES = {}
SKILL_ID_OVERRIDES = {}
ITEM_ID_OVERRIDES = {}

# Friendly import headers and internal property names from S_EquipmentDefintions.
FIELDS = [
    ("Display Name", "DisplayName_44_D561792D4D87BAEAC64C2582ACC62048"),
    ("Short Description", "ShortDescription_46_4BBFAD7248CF98639FB8AF84ADC90E2F"),
    ("Long Description", "LongDescription_48_95332B454851EB11840A9EAEEABF9486"),
    ("Thumbnail", "Thumbnail_51_B4854F5F48B47A8D961DBA8E40154E5E"),
    ("EquipmentType", "EquipmentType_2_B4DBB203499ADE02CBF3259B46D0D6FB"),
    ("Required Class", "RequiredClass_34_48D743DC4A776FABEC5E05835072C696"),
    ("Stats", "Stats_5_2230DE0B40C6A118C0BA4D9A8C3B2573"),
    ("Required Classes", "RequiredClasses_7_2F35E8374B9445C6BE0D188A4C06B5A5"),
    ("BattleSkillIDs", "BattleSkillIDs_15_A85AA4FF4D152179D5E5B1A0EF9090A1"),
    ("ExploreSkillIDs", "ExploreSkillIDs_16_0B197C8547349DB836159A8CCCC4D780"),
    ("OverDriveSkillIDs", "OverDriveSkillIDs_17_DC07EC9A46B86976E068ABB8D433EB5D"),
    ("IsRangedUnit", "IsRangedUnit_36_1953FC89470104B6693B60A4161C2E67"),
    ("AttackElement", "AttackElement_40_4F1ED4E0404FEF9BD71383AC2877E383"),
    ("StatusResistance", "StatusResistance_39_71756D3348F34609A9817BA03E4CAD68"),
    ("Upgrade Materials", "UpgradeMaterials_57_A62074A9421137B6C148799F12789DAD"),
    ("Upgraded Equipment", "UpgradedEquipment_64_CBC7B8E64B41E2BFC5905B8403E13032"),
    ("Can Be Upgraded?", "CanBeUpgraded?_61_56CA416B416350A95F3D789047241632"),
]
HEADERS = ["---"] + [label for label, _ in FIELDS]
STATS_FIELDS = [
    ("maxHP_2_41B41BC24C39ED8CDC0C58B3398B651D", "maxHP", int),
    ("maxMP_4_402F570E47A131E33E462B9D0D778331", "maxMP", int),
    ("Attack_56_24D8527844E2DFB657716C9F16A4AE21", "Attack", int),
    ("defense_16_FEA01B2E4A985F64F6B9359A13E3F651", "defense", int),
    ("speed_17_747DDA004CC2DC11462387A6F382E254", "speed", int),
    ("hit_18_04D449874401418CC9D0A6BE1FFAE2BF", "hit", int),
    ("MagicalAttack_57_C0C7731143A09315B418E7876A1F2F19", "MagicalAttack", int),
    ("magicDefense_25_FE826AEB41C90F001AB72DA466AF2D63", "magicDefense", int),
    ("actionTime_31_CD6B6FA04FF3CB1C1BA72A93110B7A28", "actionTime", float),
    ("criticalhit_34_A2D9906D42E120CD34AFD3B53AB9A137", "critical hit", float),
    ("maxOverdrivepoints_39_8FB5E0D04B921F2E7C5C1AA0ACA92420", "maxOverdrivepoints", float),
    ("MaxExhaustionPoints_42_081CED0C4E4500695A695AB3F4455538", "MaxExhaustionPoints", int),
]
STAT_COLUMN_NAMES = [
    "Max HP", "Max MP", "Attack", "Defense", "Speed", "Hit",
    "Magical Attack", "Magic Defense", "Action Time", "Critical Hit",
    "Max Overdrive Points", "Max Exhaustion Points",
]
REVIEW_FIRST_COLUMNS = ["---", "Display Name", "EquipmentType"]
REVIEW_HEADERS = (REVIEW_FIRST_COLUMNS + STAT_COLUMN_NAMES +
                  [field for field in HEADERS if field not in REVIEW_FIRST_COLUMNS] +
                  ["Blueprint Class"])
THUMBNAIL_FIELDS = [
    ("thumbnail_2_65687B0D4850E998159EC2A8AC913A92", "thumbnail"),
    ("Spritethumbnail_5_77B20F4249EA160DF53539ABF74756FC", "Sprite thumbnail"),
]
ENUMS = {
    # Numeric VALUE, internal NAME, display LABEL. Enumerator suffix != numeric value!
    "equipment": [(0, "NewEnumerator0", "Weapon"), (1, "NewEnumerator1", "Armor"),
                  (2, "NewEnumerator4", "Helm"), (3, "NewEnumerator6", "Boot"),
                  (4, "NewEnumerator7", "Accessory")],
    "unit_class": [(0, "NewEnumerator4", "All"), (1, "NewEnumerator0", "Swordsman"),
                   (2, "NewEnumerator1", "Drone"), (3, "NewEnumerator2", "Priest"),
                   (4, "NewEnumerator3", "Engineer"), (5, "NewEnumerator5", "Assassin"),
                   (6, "NewEnumerator6", "Recruited Robot")],
    "status": [(0, "NewEnumerator0", "Poison"), (1, "NewEnumerator1", "Blind"),
               (2, "NewEnumerator2", "Berserk"), (3, "NewEnumerator3", "Petrify"),
               (4, "NewEnumerator4", "Zombie"), (5, "NewEnumerator5", "Imp"),
               (6, "NewEnumerator6", "Silence")],
    "resistance": [(0, "NewEnumerator0", "Normal"), (1, "NewEnumerator1", "Resist"),
                   (2, "NewEnumerator2", "Immune")],
}
ENUM_TYPES = {
    "equipment": "E_EquipmentType",
    "unit_class": "E_UnitClass",
    "status": "E_StatusAilments",
    "resistance": "E_StatusResistanceLevel",
}
MISSING = object()


def candidates(name):
    """Real Blueprint name first, followed by Python-friendly spellings."""
    stem = re.sub(r"_\d+_[0-9A-Fa-f]{32}$", "", name)
    snake = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", stem)
    snake = re.sub(r"[^A-Za-z0-9_]", "_", snake).strip("_").lower()
    result = [name, stem, snake, stem.replace(" ", ""), stem.replace("?", "")]
    return list(dict.fromkeys(result))


def read_property(obj, names, failures=None):
    if obj is None:
        return MISSING
    for name in names:
        for candidate in candidates(name):
            try:
                return obj.get_editor_property(candidate)
            except Exception as exc:
                if failures is not None:
                    failures.append("{}: {}: {}".format(candidate, type(exc).__name__, exc))
            try:
                value = getattr(obj, candidate)
                if not callable(value):
                    return value
            except Exception:
                pass
    return MISSING


def ue_quote(value):
    return json.dumps(str(value), ensure_ascii=False)


def object_path(value):
    if value is None or value is MISSING:
        return ""
    try:
        return str(value.get_path_name())
    except Exception:
        text = str(value)
        match = re.search(r"(/(?:Game|Script|Engine)/[^'\"\s>]+)", text)
        return match.group(1) if match else ""


def asset_name(value):
    path = object_path(value) or str(value)
    name = path.rsplit("/", 1)[-1].split(".")[-1]
    return re.sub(r"_C$", "", name)


def enum_value(value, kind, display=False):
    text = str(value).strip()
    numeric = None
    if isinstance(value, bool):
        raise ValueError("A Boolean is not a {} enum value".format(kind))
    if isinstance(value, int):
        numeric = int(value)
    elif re.fullmatch(r"\d+", text):
        numeric = int(text)
    else:
        # UE4.27 returns EnumBase instances, not Python ints. Their observed
        # representation is '<E_UnitClass.SWORDSMAN: 1>'. Use the numeric VALUE
        # after the colon; it is not the suffix of NewEnumeratorN.
        wrapped = re.fullmatch(r"<([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*):\s*(-?\d+)>", text)
        if wrapped:
            if wrapped.group(1) != ENUM_TYPES[kind]:
                raise ValueError("Expected {}, received {}".format(ENUM_TYPES[kind], text))
            numeric = int(wrapped.group(3))
    for number, internal, label in ENUMS[kind]:
        if numeric is not None:
            if numeric == number:
                return label if display else internal
            continue
        # Whole enum member only: NewEnumerator1 must not match NewEnumerator10.
        member = re.search(r"(?:^|[.:<\s])" + re.escape(internal) + r"(?:$|[>:,\s])", text)
        python_label = re.sub(r"[^A-Za-z0-9]+", "_", label).strip("_").casefold()
        if member or text.casefold() in (internal.casefold(), label.casefold(), python_label):
            return label if display else internal
    raise ValueError("Unknown {} enum value: {}".format(kind, text))


def native_element(value):
    if isinstance(value, int):
        enum_obj = unreal.load_object(None, "/Script/Chapter1Reditus.EReditusElementAffinity")
        enum_type = unreal.get_type_from_enum(enum_obj) if enum_obj else None
        if not enum_type:
            raise ValueError("Cannot resolve EReditusElementAffinity value {}".format(value))
        value = enum_type.cast(value)
    text = str(value)
    match = re.search(r"(?:ReditusElementAffinity\.|EReditusElementAffinity::)([A-Za-z_][A-Za-z0-9_]*)", text)
    if match:
        return match.group(1)
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", text):
        return text
    raise ValueError("Unrecognized AttackElement: {}".format(text))


def struct_values(value, fields):
    if value is None or value is MISSING:
        raise ValueError("Missing struct value")
    tuple_error = ""
    try:
        ordered = tuple(value.to_tuple())
    except Exception as exc:
        ordered = ()
        tuple_error = "to_tuple: {}: {}".format(type(exc).__name__, exc)
    if ordered and len(ordered) != len(fields):
        raise ValueError("Struct layout changed: expected {} members, found {}".format(len(fields), len(ordered)))
    values, failures = [], []
    for field in fields:
        names = [field[0], field[1]]
        values.append(read_property(value, names, failures))
    if any(v is MISSING for v in values):
        if len(ordered) == len(fields):
            values = [ordered[i] if v is MISSING else v for i, v in enumerate(values)]
    if any(v is MISSING for v in values):
        missing = [field[1] for field, member in zip(fields, values) if member is MISSING]
        details = "; ".join(([tuple_error] if tuple_error else []) + list(dict.fromkeys(failures)))
        raise ValueError("Cannot read struct members {}. Unreal property errors: {}".format(
            ", ".join(missing), details or "No members returned."))
    return values


def stats_numbers(value):
    values = struct_values(value, STATS_FIELDS)
    numbers = []
    for (raw, _, number_type), number in zip(STATS_FIELDS, values):
        number = number_type(number)
        if not math.isfinite(number):
            raise ValueError("Non-finite stat {}".format(raw))
        numbers.append(number)
    return numbers


def stats_from_numbers(numbers):
    parts = ["{}={}".format(raw, number)
             for (raw, _, _), number in zip(STATS_FIELDS, numbers)]
    return "(" + ",".join(parts) + ")"


def stats_text(value):
    return stats_from_numbers(stats_numbers(value))


def thumbnail_text(value):
    parts = []
    for (raw, _), obj in zip(THUMBNAIL_FIELDS, struct_values(value, THUMBNAIL_FIELDS)):
        path = object_path(obj)
        if obj is not None and not path:
            raise ValueError("Cannot resolve thumbnail asset reference")
        if path:
            class_name = str(obj.get_class().get_name())
            exported = "{}'{}'".format(class_name, path)
        else:
            exported = "None"
        parts.append(raw + "=" + exported)
    return "(" + ",".join(parts) + ")"


def as_array(value):
    if value is None:
        return []
    if isinstance(value, (str, bytes)) or hasattr(value, "to_tuple"):
        raise ValueError("Expected an array, received {}".format(type(value).__name__))
    return list(value)


def bool_text(value):
    if isinstance(value, bool) or value in (0, 1):
        return "True" if bool(value) else "False"
    if str(value).casefold() in ("true", "false"):
        return str(value).capitalize()
    raise ValueError("Expected a Boolean, received {}".format(value))


ZERO_STATS = "(" + ",".join(raw + "=0" for raw, _, _ in STATS_FIELDS) + ")"
DEFAULTS = {
    "Display Name": "", "Short Description": "", "Long Description": "",
    "Thumbnail": "(" + ",".join(raw + "=None" for raw, _ in THUMBNAIL_FIELDS) + ")",
    "EquipmentType": "Weapon", "Required Class": "()", "Stats": ZERO_STATS,
    "Required Classes": ZERO_STATS, "BattleSkillIDs": "()", "ExploreSkillIDs": "()",
    "OverDriveSkillIDs": "()", "IsRangedUnit": "False", "AttackElement": "Phys",
    "StatusResistance": "()", "Upgrade Materials": "()", "Upgraded Equipment": "None",
    "Can Be Upgraded?": "False",
}


class Exporter:
    def __init__(self):
        self.issues = []
        self.errors = 0
        self.existing = {}
        self.ids = {"equipment": {}, "skill": {}, "item": {}}
        self.skill_classes = {}
        self.blueprint_count = 0
        self.review_stats = {}
        self.blueprint_sources = {}

    def issue(self, row, field, message, error=False):
        self.errors += int(error)
        self.issues.append(("ERROR" if error else "NOTE", row, field, message))
        if error:
            unreal.log_error("[Reditus] {} | {} | {}".format(row, field, message))

    def column(self, table, names, expected):
        for name in names:
            for candidate in candidates(name):
                try:
                    values = list(unreal.DataTableFunctionLibrary.get_data_table_column_as_string(table, candidate))
                    if len(values) == expected:
                        return [str(v) for v in values]
                except Exception:
                    pass
        return None

    def load_tables(self):
        table = unreal.load_asset(EQUIPMENT_TABLE)
        if table is None:
            raise RuntimeError("DT_Equipment not found: " + EQUIPMENT_TABLE)
        row_struct = table.get_editor_property("row_struct")
        if row_struct is None or row_struct.get_name() != "S_EquipmentDefintions":
            raise RuntimeError("DT_Equipment does not use the screenshot's S_EquipmentDefintions struct.")
        names = [str(v) for v in unreal.DataTableFunctionLibrary.get_data_table_row_names(table)]
        self.existing = {name: {"---": name} for name in names}
        for label, raw in FIELDS:
            values = self.column(table, [raw, label], len(names)) if names else []
            if values is None:
                raise RuntimeError("Cannot read existing DT_Equipment column '{}'. Its struct may have changed.".format(label))
            for name, value in zip(names, values):
                self.existing[name][label] = value
        self.ids["equipment"] = {n.casefold(): n for n in names}
        self.ids["item"].update(self.ids["equipment"])
        for short_name, kind in [("DT_Skills", "skill"), ("DT_UsableItems", "item"),
                                 ("DT_KeyItems", "item"), ("DT_Recipes", "item")]:
            reference_table = unreal.load_asset(TABLE_ROOT + "/" + short_name)
            if reference_table is None:
                continue
            reference_names = [str(v) for v in unreal.DataTableFunctionLibrary.get_data_table_row_names(reference_table)]
            for name in reference_names:
                self.ids[kind][name.casefold()] = name
            if kind == "skill" and reference_names:
                for raw in ["LegacySkill_39_B3913477462B7C3F7B99CF987B956D51", "LegacyExploreSkill_54_CB121B5F4D079372A69642AB3C372101"]:
                    values = self.column(reference_table, [raw, re.sub(r"_\d+_[0-9A-F]{32}$", "", raw)], len(reference_names))
                    for name, value in zip(reference_names, values or []):
                        path = object_path(value)
                        if path:
                            previous = self.skill_classes.get(path.casefold())
                            if previous is not None and previous != name:
                                raise RuntimeError("One legacy skill class maps to multiple DT_Skills rows: " + path)
                            self.skill_classes[path.casefold()] = name

    def row_id(self, value, kind, row="", field=""):
        if value is None or str(value) in ("", "None"):
            return "None"
        path, name = object_path(value), asset_name(value)
        overrides = {"equipment": ROW_ID_OVERRIDES, "skill": SKILL_ID_OVERRIDES, "item": ITEM_ID_OVERRIDES}[kind]
        for key in [path, re.sub(r"_C$", "", path), name]:
            if key in overrides:
                return str(overrides[key])
        if kind == "skill" and path.casefold() in self.skill_classes:
            return self.skill_classes[path.casefold()]
        suggested = re.sub(r"^bp_", "", name, flags=re.I)
        known = self.ids[kind].get(suggested.casefold())
        if known:
            return known
        if not path:
            return str(value)  # Already an explicit Name ID, not a Blueprint reference.
        if row and kind in ("skill", "item"):
            self.issue(row, field, "Derived ID '{}' from {}; no matching current reference-table row. Verify this ID before using it.".format(suggested, path))
        return suggested

    def fallback(self, row, field, message):
        if row in self.existing:
            self.issue(row, field, message + " Kept the existing DT_Equipment value.")
            return self.existing[row][field]
        self.issue(row, field, message + " Used the explicit default: " + DEFAULTS[field])
        return DEFAULTS[field]

    def equipment_kind(self, cls, cdo, bases, row):
        for label, parent in bases.items():
            if parent and unreal.MathLibrary.class_is_child_of(cls, parent):
                saved = read_property(cdo, ["equipmentType", "EquipmentType"])
                if saved is not MISSING:
                    try:
                        saved_label = enum_value(saved, "equipment", display=True)
                    except ValueError:
                        saved_label = str(saved)
                    if saved_label != label:
                        self.issue(row, "EquipmentType", "Blueprint default '{}' differs from its {} base; exported the category base's type.".format(saved_label, label))
                return label
        value = read_property(cdo, ["equipmentType", "EquipmentType"])
        if value is MISSING:
            raise ValueError("No supported category parent or equipmentType property")
        label = enum_value(value, "equipment", display=True)
        self.issue(row, "EquipmentType", "No category base matched (e.g. a shield). Copied the Blueprint's actual EquipmentType: " + label)
        return label

    def build_row(self, cls, cdo, bases, row):
        result = dict(self.existing.get(row, dict(DEFAULTS)))
        result["---"] = row

        def put(field, names, serialize, required=False):
            failures = []
            value = read_property(cdo, names, failures)
            if value is MISSING:
                if required:
                    details = "; ".join(dict.fromkeys(failures)) or "No value returned."
                    raise ValueError("Cannot read Class Defaults property for {}. Unreal property errors: {}".format(field, details))
                result[field] = self.fallback(row, field, "No corresponding Blueprint default.")
            else:
                # Existing-but-unreadable values must not silently become zero/None.
                try:
                    result[field] = serialize(value)
                except Exception as exc:
                    raise ValueError("{} could not be serialized from {}: {}: {}".format(
                        field, type(value).__name__, type(exc).__name__, exc)) from exc

        put("Display Name", ["name", "DisplayName", "Display Name"], str, True)
        put("Short Description", ["shortDescription", "Short Description"], str, True)
        put("Long Description", ["longDescription", "Long Description"], str, True)
        put("Thumbnail", ["thumbnail", "Thumbnail"], thumbnail_text, True)
        def export_stats(value):
            # One read supplies both files: the review's numeric cells are
            # exactly the values serialized into the DataTable Stats struct.
            numbers = stats_numbers(value)
            self.review_stats[row] = dict(zip(STAT_COLUMN_NAMES, numbers))
            return stats_from_numbers(numbers)
        put("Stats", ["stats", "Stats"], export_stats, True)
        result["EquipmentType"] = self.equipment_kind(cls, cdo, bases, row)
        put("Required Class", ["requiredClass", "RequiredClass", "Required Class", "requiredClasses"],
            lambda value: "(" + ",".join(enum_value(v, "unit_class") for v in as_array(value)) + ")", True)

        # Legacy requiredClasses is an enum array, not the separate S_UnitStats column.
        required_stats = read_property(cdo, ["RequiredStats", "requiredStats", "Required Classes", "RequiredClasses"])
        if required_stats is not MISSING and hasattr(required_stats, "to_tuple"):
            result["Required Classes"] = stats_text(required_stats)
        else:
            result["Required Classes"] = self.fallback(row, "Required Classes", "This column is S_UnitStats, not the class-restriction array.")

        for field, properties in [
            ("BattleSkillIDs", ["BattleSkillIDs", "battleSkills"]),
            ("ExploreSkillIDs", ["ExploreSkillIDs", "exploreSkills"]),
            ("OverDriveSkillIDs", ["OverDriveSkillIDs", "OverdriveTechs"]),
        ]:
            def skills(value, field=field):
                ids = []
                for skill in as_array(value):
                    name = self.row_id(skill, "skill", row, field)
                    if name != "None":
                        ids.append(name)
                    else:
                        self.issue(row, field, "Ignored a null skill reference.")
                return "(" + ",".join(ue_quote(v) for v in ids) + ")"
            put(field, properties, skills)

        put("IsRangedUnit", ["IsRangedUnit", "isRangedUnit", "IsRangeUnit"], bool_text)
        put("AttackElement", ["AttackElement", "Attack Element"], native_element)
        put("StatusResistance", ["StatusResistance", "Status Resistance"],
            lambda value: "(" + ",".join("({},{})".format(enum_value(k, "status"), enum_value(v, "resistance"))
                                          for k, v in value.items()) + ")")
        immunities = read_property(cdo, ["Immunities"])
        if immunities is not MISSING and immunities and read_property(cdo, ["StatusResistance", "Status Resistance"]) is MISSING:
            # In this project a populated legacy Immunities default can be one
            # BlueprintGeneratedClass, not an array (MetalHelm/TuskHelm). This
            # is only a report note; keep the modern resistance value above.
            immunity_refs = ([immunities] if callable(getattr(immunities, "get_path_name", None))
                             else as_array(immunities))
            self.issue(row, "StatusResistance", "Legacy Immunities contains buff class references; these are not automatically guessed into resistance enum keys: " + ", ".join(object_path(v) or str(v) for v in immunity_refs))

        def materials(value):
            counts = {}
            for material, quantity in value.items():
                name = self.row_id(material, "item", row, "Upgrade Materials")
                if name == "None":
                    raise ValueError("Null upgrade-material reference")
                if isinstance(quantity, bool) or int(quantity) != quantity or int(quantity) < 0:
                    raise ValueError("Invalid upgrade-material quantity: {}".format(quantity))
                key = name.casefold()
                if key in counts:
                    raise ValueError("Two material references resolve to the same Name ID: " + name)
                counts[key] = (name, int(quantity))
            return "(" + ",".join("({},{})".format(ue_quote(name), quantity)
                                  for name, quantity in sorted(counts.values())) + ")"

        put("Upgrade Materials", ["UpgradeMaterials", "Upgrade Materials"], materials)
        put("Upgraded Equipment", ["UpgradedEquipment", "Upgraded Equipment", "UpgradeItem"],
            lambda value: self.row_id(value, "equipment", row, "Upgraded Equipment"))
        put("Can Be Upgraded?", ["Can Be Upgraded?", "CanBeUpgraded?", "CanBeUpgraded", "can_be_upgraded"], bool_text)
        return result

    def run(self):
        if not hasattr(unreal, "EditorAssetLibrary"):
            raise RuntimeError("Enable Editor Scripting Utilities, then restart Unreal.")
        self.load_tables()
        equipment_base = unreal.EditorAssetLibrary.load_blueprint_class(EQUIPMENT_ROOT + "/BP_EquipmentBase")
        if not equipment_base:
            raise RuntimeError("BP_EquipmentBase could not be loaded.")
        bases = {name: unreal.EditorAssetLibrary.load_blueprint_class(path) for name, path in BASE_PATHS.items()}
        if any(base is None for base in bases.values()):
            raise RuntimeError("A category base could not be loaded. Check BASE_PATHS at the top of this script.")
        records, used = dict(self.existing), {}
        classes = []
        for path in sorted(unreal.EditorAssetLibrary.list_assets(ITEM_ROOT, recursive=True, include_folder=False)):
            # Equipment meshes are actors used by equipment, not item records.
            if path.casefold().startswith((EQUIPMENT_ROOT + "/EquipmentMesh/").casefold()):
                continue
            name = asset_name(path)
            if name.casefold() in SKIP_NAMES:
                continue
            data = unreal.EditorAssetLibrary.find_asset_data(path)
            if str(data.get_editor_property("asset_class")) != "Blueprint":
                continue
            try:
                cls = unreal.EditorAssetLibrary.load_blueprint_class(path)
                if cls and unreal.MathLibrary.class_is_child_of(cls, equipment_base):
                    classes.append((path, cls))
            except Exception as exc:
                self.issue(name, "Blueprint", "Could not check class: " + str(exc), True)
        if not classes:
            raise RuntimeError("No concrete equipment Blueprints found under " + ITEM_ROOT)
        # Build the ID index first, so UpgradeItem can point to another new equipment row.
        for path, cls in classes:
            row = self.row_id(path, "equipment")
            if not row or row == "None":
                raise RuntimeError("Empty equipment row ID for " + path)
            if row.casefold() in used:
                raise RuntimeError("Duplicate equipment row ID '{}': {} and {}. Set ROW_ID_OVERRIDES.".format(row, used[row.casefold()], path))
            used[row.casefold()] = path
            self.ids["equipment"][row.casefold()] = row
            self.ids["item"][row.casefold()] = row
        for path, cls in classes:
            row = self.row_id(path, "equipment")
            try:
                cdo = unreal.get_default_object(cls)
                if cdo is None:
                    raise ValueError("Class Default Object unavailable")
                records[row] = self.build_row(cls, cdo, bases, row)
                self.blueprint_sources[row] = object_path(cls) or path
                self.blueprint_count += 1
            except Exception as exc:
                self.issue(row, "Blueprint", "Export failed for {}: {}".format(path, exc), True)

        output_dir = os.path.abspath(os.path.join(str(unreal.Paths.project_saved_dir()), "EquipmentExports"))
        os.makedirs(output_dir, exist_ok=True)
        output_path, review_path = None, None
        if not self.errors:
            output_path = os.path.join(output_dir, "DT_Equipment_Import.csv")
            review_path = os.path.join(output_dir, "DT_Equipment_Review.csv")
            temporary = output_path + ".tmp"
            review_temporary = review_path + ".tmp"
            with open(temporary, "w", encoding="utf-8-sig", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=HEADERS, extrasaction="raise")
                writer.writeheader()
                for row in sorted(records, key=str.casefold):
                    writer.writerow(records[row])
            with open(review_temporary, "w", encoding="utf-8-sig", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=REVIEW_HEADERS, extrasaction="raise")
                writer.writeheader()
                for row in sorted(self.blueprint_sources, key=str.casefold):
                    review_row = dict(records[row])
                    review_row.update(self.review_stats[row])
                    review_row["Blueprint Class"] = self.blueprint_sources[row]
                    writer.writerow(review_row)
            os.replace(temporary, output_path)
            os.replace(review_temporary, review_path)
        report_path = os.path.join(output_dir, "DT_Equipment_Export_Report.txt")
        with open(report_path, "w", encoding="utf-8") as stream:
            stream.write("Reditus equipment Class Defaults export\nTime: {}\n".format(datetime.datetime.now().isoformat()))
            stream.write("Blueprints read successfully: {}\nCSV rows written: {}\nErrors: {}\nCSV: {}\n\n".format(
                self.blueprint_count, len(records) if output_path else 0, self.errors,
                output_path or "NOT CREATED: Blueprint reads failed"))
            stream.write("Existing table-only rows are retained. Fields absent from BPs preserve existing cells or use explicit defaults.\n")
            stream.write("Required Class = E_UnitClass array; Required Classes = S_UnitStats, as in the screenshot.\n")
            stream.write("IDs use existing table mappings first; unverified class-to-ID conversions are listed below.\n")
            stream.write("AttackElement/ranged flags/resistances are not inferred from asset names or graph pins.\n")
            stream.write("Excel review: {}\n".format(review_path or "NOT CREATED: Blueprint reads failed"))
            stream.write("Review.csv lists only Blueprint-backed rows and gives all 12 Stats members separate numeric columns.\n")
            stream.write("Import.csv keeps the original 17 DataTable fields, with equipment stats inside the Stats struct.\n")
            table_only = sorted(set(records) - set(self.blueprint_sources), key=str.casefold)
            if table_only:
                stream.write("Existing table-only rows (not present in the Blueprint review): {}\n".format(
                    ", ".join(table_only)))
            for level, row, field, message in self.issues:
                stream.write("[{}] {} | {} | {}\n".format(level, row, field, message))
        unreal.log("[Reditus] Read {} equipment Blueprints successfully; {} CSV rows written.".format(
            self.blueprint_count, len(records) if output_path else 0))
        if output_path:
            unreal.log("[Reditus] CSV: " + output_path)
            unreal.log("[Reditus] Excel review (separate stat columns): " + review_path)
        unreal.log("[Reditus] Report: " + report_path)
        if self.errors:
            # Remove an older successful output from this script so a stale CSV
            # cannot be mistaken for the current, unsuccessful export.
            for source, destination in [
                ("DT_Equipment_Import.csv", "DT_Equipment_Previous_Import.csv"),
                ("DT_Equipment_INCOMPLETE.csv", "DT_Equipment_Previous_INCOMPLETE.csv"),
                ("DT_Equipment_Review.csv", "DT_Equipment_Previous_Review.csv"),
            ]:
                stale_source = os.path.join(output_dir, source)
                if os.path.isfile(stale_source):
                    os.replace(stale_source, os.path.join(output_dir, destination))
            unreal.log_error("[Reditus] {} errors. No CSV created. The specific failures are above and in: {}".format(self.errors, report_path))
        elif self.issues:
            unreal.log_warning("[Reditus] {} notes about retained/defaulted values or ID mappings; see the report.".format(len(self.issues)))
        return output_path


def main():
    try:
        return Exporter().run()
    except Exception as exc:
        unreal.log_error("[Reditus equipment export] " + str(exc))
        raise


if __name__ == "__main__":
    main()
