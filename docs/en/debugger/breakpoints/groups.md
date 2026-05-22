# Breakpoint groups

Groups form a hierarchical tree on top of breakpoints. They serve for
organization, bulk enable / disable (cascade) and visual structuring in
the UI.

## Purpose

- **Organization** - logical grouping of BPs into thematic groups
  (e.g. "ROM monitor", "Game engine", "FDC trace"). A tree view is more
  readable than a flat list of dozens of BPs.
- **Bulk enable / disable** - cascade flag. Disabling a parent group
  disables all BPs in the branch without needing to change per-BP
  `enabled`.
- **Color coding** - per-group background / text colors (the label
  color), inherited in the UI group label. Per-BP colors are
  independent (= no inheritance, see "Color semantics" below).
- **Persistence** - groups + parent links are stored in the `.bpt` JSON
  `groups` section in parallel with breakpoints.

## Hierarchy model

### Data model

A group has the following attributes:

- **ID** - unique positive number.
- **parent** - the ID of the parent group or -1 (= root).
- **order** - display order among siblings.
- **enabled** - cascade flag.
- **name** - the group name.
- **bg_rgb**, **fg_rgb** - background and text label color.

A breakpoint has a symmetric `parent` field with the same semantics
(-1 = root, otherwise group ID).

### Parent / child relationships

- **Root** is virtual (has no entity) - represented by the value
  `parent = -1`.
- A **Group** may have a parent = another group (parent >= 0) or root
  (parent = -1).
- A **Breakpoint** may have a parent = a group (parent >= 0) or root
  (parent = -1).
- **Multiple parenthood** = unsupported (= one parent ID per child).

### Nesting depth limit

The maximum tree depth is 32 levels. The limit is checked by:

- A load-time scan of the parent chain (validation when loading
  `.bpt`).
- A runtime recursion when computing cascade enabled (a
  belt-and-suspenders guard in case load validation failed).

A parent change via the UI / drag-drop has its own runtime guard
against cycles.

## Cascade enable semantics

### Algorithm

The effective enabled state of a BP is computed by iterating from its
parent group up the parent chain. Rules:

- **AND** across the entire path - all groups from the BP up to root
  must be `enabled = true`.
- **Short circuit** - the first disabled group on the path is enough
  to exclude the BP.
- **The per-BP `enabled` flag is AND-ed separately** - effective
  enabled = `bpt.enabled && (all groups on the path enabled)`.
- **Root is always enabled** - there is no entity to disable.
- **A non-existent group** (= a dangling parent ID after manual editing
  of `.bpt`) behaves as root (= treat as enabled). Not an error.

### Consequence for the hot path

On every BP enforce hook the parent chain is traversed recursively.
The cascade enabled state is therefore precomputed into an internal
bytemap on every mutation of a group/BP enabled flag - the PC_EXEC
hot path then tests only a one-shot bytemap, not a recursion.

For per-instruction BP (GLOBAL) and IRQ / HW_EVENT enforce, the
cascade test is called at runtime, but only after a short-circuit
test of the global active flag.

## Color semantics

**Inheritance DOES NOT EXIST.** A group and a breakpoint each have
their own `bg_rgb` / `fg_rgb` - the tree view UI renders them
independently:

- The group label uses the group's own bg/fg colors.
- The breakpoint label uses the breakpoint's own bg/fg colors.

Default colors of a newly created group / BP:

- BG = `0x000000` (black)
- FG = `0xFFFFFF` (white)

If the user wants to visually unify a group and its BPs, the colors
must be set on both levels manually.

### Disabled cascade overlay

If a parent group is disabled, the tree view draws a strikethrough
overlay (a line across the label) on every BP in the given branch.

## UI workflow

### Tree view

The main `Breakpoints` window renders the tree. Per level:

- **Group** = TreeNode openable by the arrow on the left, clicking
  the label = select.
- **Breakpoint** = row with double-click = open the Edit panel.

### Operations - context menu (right-click)

| Item                   | Action |
|------------------------|--------|
| Expand All             | Expands all TreeNodes |
| Collapse All           | Collapses all TreeNodes |
| Add Breakpoint Event...| Opens the Edit panel for a new BP |
| Add Group...           | Opens the Edit panel for a new group |
| Edit row...            | Opens the Edit panel for the selected item |
| Unparent               | Moves the selected item to root (parent = -1) |
| Delete Row/Branch      | Deletes the selected item |
| Delete All             | Deletes everything (BPs + groups) |

### Drag-drop reparenting

Source = any row (group or BP).

Target sites:

- **Group** - drop on a group label = reparent the dropped item under
  the target group.
- **Root** - drop below the tree view (outside all groups) = reparent
  to root.

## Validation - cycle prevention

On a parent change (via UI or drag-drop) the following happens:

1. **Self-parent reject** - a group cannot be its own parent.
2. **Non-existing parent reject** - the target group must exist.
3. **Cycle scan** - iteration from the new parent chain upwards. If
   it hits the group being changed, that would mean a cycle = reject.
4. Iteration is bounded by a depth fuse for the case of an existing
   cycle from earlier manual editing of `.bpt`.

For BP, no cycle scan is needed - a BP cannot be a parent (only
`parent = group ID | -1`).

## Group operations

### Add

Creates a group with default colors and `enabled = true`. Returns the
new ID or -1 on allocation failure.

### Remove

When a group is deleted:

1. Groups with this group as their parent -> reparented to root.
2. BPs with this group as their parent -> reparented to root.
3. The group itself is removed.

**Consequence:** deleting a group **never deletes its descendants**.
A cascade delete (= delete a group and all BPs inside) must be done
explicitly by the UI by deleting every child before the group.

### Set enabled / name / colors / order / parent

Per-field setters. Changes to `enabled` and `parent` invalidate the
effective-enabled cache (= recompute bytemap for the hot path).

## Persistence

Groups serialize into the top-level JSON section `groups`. Per-group
object:

```json
{
  "id": 1,
  "parent_id": -1,
  "name": "ROM monitor",
  "enabled": true,
  "order": 1.0,
  "color_bg": 0,
  "color_fg": 16777215
}
```

Key order is stable (the saver always identical). The loader is
permissive - missing keys get defaults (see `persistence.md`
per-key table).

Breakpoints reference a group through their own `parent_id` field.
Loader order:

1. First load all groups (= parent_id exists).
2. Then load the breakpoints (= their parent_id reference is valid).
3. Groups with a non-existent parent_id (= dangling) behave as root
   (cascade enable returns true).

## Edge cases

- **Group without a parent (-1)** - top-level / root. The only option
  for a root group.
- **Cycle after manual editing of `.bpt`** - the load-time cycle scan
  detects:
  - Self-loop (= grp.parent == grp.id)
  - A closed ring across multiple groups (depth limit 32)
  - Return to the current group ID in the parent chain

  On detection: warning to stderr + `parent = -1` (= reparent to root).
  The belt-and-suspenders runtime guard also catches the case where
  the load validation would have failed (depth fallback = treat as
  enabled, warning).
- **A group with itself as the parent** - caught by load validation
  (= reparent to root + warning). The runtime set_parent additionally
  rejects it already on write.
- **A deleted group with BP children** - children are reparented to
  root, they stay active (= NO cascade delete).
- **Order field** - used for sorting in the UI tree view. Primarily
  order, then alphabet, then ID.

## Related documents

- `README.md` - subsystem architecture
- `types.md` - catalogue of 9 BP types (each BP can be in a group)
- `persistence.md` - JSON schema for the `groups` section + the
  `parent_id` field
- `match-modes.md` - per-BP match modes (independent of groups)
