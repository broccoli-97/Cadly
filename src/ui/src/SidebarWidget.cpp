#include "SidebarWidget.h"

#include "IconUtils.h"
#include "Popover.h"
#include "PropertiesPanel.h"
#include "cadly/ui/ThemeTokens.h"

#include <QGuiApplication>
#include <QCursor>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QSortFilterProxyModel>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QVBoxLayout>

#include <functional>

namespace cadly::ui {

namespace {
constexpr int kRoleNodeIndex = Qt::UserRole + 1;
constexpr int kRoleTriCount  = Qt::UserRole + 2;
constexpr int kRoleVisible   = Qt::UserRole + 3;
constexpr int kRoleGhosted   = Qt::UserRole + 4;

constexpr int kRowHeight   = 28;
constexpr int kGlyphSize   = 16;   // eye / info hit targets
constexpr int kGlyphPad    = 4;
} // namespace

// Paints one source-list row: selection pill, name, right-aligned triangle
// count, and — on hover or when the node is hidden — the eye and ⓘ glyphs.
// Rows outside the current isolate focus paint grayed (kRoleGhosted), the
// tree-side twin of the viewport's ghost veil. Click handling for the glyphs
// lives in editorEvent so the tree view's selection behaviour stays
// untouched everywhere else in the row.
class SidebarDelegate final : public QStyledItemDelegate {
public:
  explicit SidebarDelegate(SidebarWidget* owner, QTreeView* view)
    : QStyledItemDelegate(view), owner_(owner), view_(view) {}

  QSize sizeHint(const QStyleOptionViewItem&,
                 const QModelIndex&) const override {
    return {120, kRowHeight};
  }

  // Row-glyph hit boxes, shared with SidebarWidget's double-click handler so
  // a rapid eye/ⓘ click never reads as an isolate request.
  static QRect eye_rect(const QRect& row) {
    return {row.right() - 2 * (kGlyphSize + kGlyphPad) - 4,
            row.top() + (row.height() - kGlyphSize) / 2,
            kGlyphSize, kGlyphSize};
  }
  static QRect info_rect(const QRect& row) {
    return {row.right() - (kGlyphSize + kGlyphPad) - 2,
            row.top() + (row.height() - kGlyphSize) / 2,
            kGlyphSize, kGlyphSize};
  }

  void paint(QPainter* p, const QStyleOptionViewItem& opt,
             const QModelIndex& index) const override {
    const auto& t = tokens();
    p->save();
    p->setRenderHint(QPainter::Antialiasing);

    const bool selected = opt.state.testFlag(QStyle::State_Selected);
    const bool hovered  = opt.state.testFlag(QStyle::State_MouseOver);
    const bool visible  = index.data(kRoleVisible).toBool();
    const bool ghosted  = index.data(kRoleGhosted).toBool();

    if (selected) {
      p->setPen(Qt::NoPen);
      p->setBrush(t.selection_bg);
      p->drawRoundedRect(QRectF(opt.rect).adjusted(2, 1, -2, -1), 5, 5);
    } else if (hovered) {
      p->setPen(Qt::NoPen);
      p->setBrush(t.control_bg);
      p->drawRoundedRect(QRectF(opt.rect).adjusted(2, 1, -2, -1), 5, 5);
    }

    // Right-hand cluster: [eye] [info] [count]. The count yields to the
    // glyphs on hover so the row never jumps in height, only in detail.
    int right = opt.rect.right() - 6;

    QColor faded = t.text3;
    faded.setAlphaF(faded.alphaF() * 0.55);

    const QString count = index.data(kRoleTriCount).toString();
    if (!count.isEmpty() && !hovered) {
      p->setFont(mono_font(11));
      p->setPen(ghosted ? faded : t.text3);
      p->drawText(QRect(opt.rect.left(), opt.rect.top(),
                        right - opt.rect.left(), opt.rect.height()),
                  Qt::AlignRight | Qt::AlignVCenter, count);
      const QFontMetrics fm(mono_font(11));
      right -= fm.horizontalAdvance(count) + 8;
    }

    if (hovered || !visible) {
      const QRect eye = eye_rect(opt.rect);
      const QRect inf = info_rect(opt.rect);
      if (hovered) {
        p->setPen(t.text2);
        p->setFont(ui_font(12, QFont::DemiBold));
        draw_glyph_icon(p, inf, QStringLiteral("misc/info"));
      }
      draw_glyph_icon(p, eye,
                      visible ? QStringLiteral("action/eye")
                              : QStringLiteral("action/eye-crossed"),
                      visible ? t.text2 : t.warn);
      right = eye.left() - 6;
    }

    // A stable part glyph gives dense assemblies a second scan channel beyond
    // indentation alone (matching the prototype's source-list rows).
    const QRect part_icon(opt.rect.left() + 3,
                          opt.rect.top() + (opt.rect.height() - 14) / 2,
                          14, 14);
    const QColor part_color = selected ? t.accent
                            : ghosted  ? faded
                                       : t.text3;
    draw_glyph_icon(p, part_icon, QStringLiteral("shape/cube"), part_color);

    // Name, elided to whatever space is left. Hidden and ghosted rows both
    // drop to the tertiary tone; the eye glyph disambiguates which is which.
    const QString name = index.data(Qt::DisplayRole).toString();
    p->setFont(ui_font(12, selected ? QFont::DemiBold : QFont::Normal));
    p->setPen(visible && !ghosted ? t.text1 : t.text3);
    const QFontMetrics fm(p->font());
    const QRect name_rect(part_icon.right() + 5, opt.rect.top(),
                          right - part_icon.right() - 9, opt.rect.height());
    p->drawText(name_rect, Qt::AlignLeft | Qt::AlignVCenter,
                fm.elidedText(name, Qt::ElideRight, name_rect.width()));
    p->restore();
  }

  bool editorEvent(QEvent* event, QAbstractItemModel*,
                   const QStyleOptionViewItem& opt,
                   const QModelIndex& index) override {
    // A double-click's second press arrives as MouseButtonDblClick, not
    // MouseButtonPress. Handle both so rapid eye clicks each count as a
    // toggle, and a rapid ⓘ click doesn't stack a second popover.
    const bool press    = event->type() == QEvent::MouseButtonPress;
    const bool dblclick = event->type() == QEvent::MouseButtonDblClick;
    if (!press && !dblclick) return false;
    auto* me = static_cast<QMouseEvent*>(event);
    if (me->button() != Qt::LeftButton) return false;

    const auto node = index.data(kRoleNodeIndex);
    if (!node.isValid()) return false;
    const auto node_index = node.toUInt();

    if (eye_rect(opt.rect).contains(me->pos())) {
      owner_->toggle_eye(node_index,
                         me->modifiers().testFlag(Qt::AltModifier));
      return true;   // consumed: don't let the click change selection
    }
    if (info_rect(opt.rect).contains(me->pos())) {
      if (press) {
        const QRect global(view_->viewport()->mapToGlobal(opt.rect.topLeft()),
                           opt.rect.size());
        owner_->show_get_info(node_index, global);
      }
      return true;
    }
    return false;
  }

private:
  static void draw_glyph_icon(QPainter* p, const QRect& r, const QString& name,
                              const QColor& color = tokens().text2) {
    themed_icon(name, color, color).paint(p, r);
  }

  SidebarWidget* owner_;
  QTreeView*     view_;
};

SidebarWidget::SidebarWidget(QWidget* parent) : QWidget(parent) {
  setAutoFillBackground(false);
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(8, 4, 8, 8);
  outer->setSpacing(2);

  auto* header = new QHBoxLayout();
  header->setContentsMargins(4, 0, 0, 0);
  auto* title = new QLabel(tr("MODEL"), this);
  title->setFont(ui_font(11, QFont::DemiBold));
  header->addWidget(title);
  header->addStretch();

  filter_ = new QLineEdit(this);
  filter_->setPlaceholderText(tr("Filter parts"));
  filter_->setClearButtonEnabled(true);
  filter_->setFixedWidth(130);
  filter_->setFixedHeight(24);
  header->addWidget(filter_);
  outer->addLayout(header);

  tree_  = new QTreeView(this);
  model_ = new QStandardItemModel(tree_);
  proxy_ = new QSortFilterProxyModel(tree_);
  proxy_->setSourceModel(model_);
  proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
  proxy_->setRecursiveFilteringEnabled(true);
  tree_->setModel(proxy_);
  tree_->setHeaderHidden(true);
  tree_->setUniformRowHeights(true);
  tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
  tree_->setMouseTracking(true);   // hover state for the delegate glyphs
  tree_->setFrameShape(QFrame::NoFrame);
  tree_->setIndentation(14);
  // Double-click is the isolate gesture; expand/collapse stays on the
  // branch indicator so entering isolate never also folds an assembly.
  tree_->setExpandsOnDoubleClick(false);
  tree_->viewport()->setAutoFillBackground(false);
  tree_->setItemDelegate(new SidebarDelegate(this, tree_));
  // Blank-area clicks (below the last row) deselect — see eventFilter.
  tree_->viewport()->installEventFilter(this);
  outer->addWidget(tree_, 1);

  connect(filter_, &QLineEdit::textChanged, proxy_,
          [this](const QString& text) {
            proxy_->setFilterFixedString(text);
            if (!text.isEmpty()) tree_->expandAll();
          });

  connect(tree_->selectionModel(), &QItemSelectionModel::currentChanged, this,
          [this](const QModelIndex& current, const QModelIndex&) {
            if (!current.isValid()) {
              apply_selection(scene::Node::kInvalid);
              // Deselect reaches the Properties tab too, so it resets
              // instead of pinning the last-selected part forever.
              emit node_selected(scene::Node::kInvalid);
              return;
            }
            const auto v = current.data(kRoleNodeIndex);
            if (!v.isValid()) return;
            apply_selection(v.toUInt());
            emit node_selected(v.toUInt());
          });

  // Double-click isolates the row's subtree (again on the isolate root to
  // exit). doubleClicked fires before the delegate can consume the event,
  // so filter out double-clicks that land on the eye/ⓘ glyphs — those are
  // rapid toggles, not an isolate request.
  connect(tree_, &QTreeView::doubleClicked, this,
          [this](const QModelIndex& index) {
            if (!index.isValid()) return;
            if (!(QGuiApplication::mouseButtons() & Qt::LeftButton)) return;
            const auto v = index.data(kRoleNodeIndex);
            if (!v.isValid()) return;
            const QPoint pos =
              tree_->viewport()->mapFromGlobal(QCursor::pos());
            const QRect row = tree_->visualRect(index);
            if (SidebarDelegate::eye_rect(row).contains(pos) ||
                SidebarDelegate::info_rect(row).contains(pos)) {
              return;
            }
            const auto idx = v.toUInt();
            set_isolate(idx == isolate_node_ ? scene::Node::kInvalid : idx);
          });

  connect(&ThemeManager::instance(), &ThemeManager::changed,
          this, QOverload<>::of(&QWidget::update));
}

void SidebarWidget::paintEvent(QPaintEvent*) {
  QPainter p(this);
  const auto& t = tokens();
  p.fillRect(rect(), t.sidebar_bg);
  p.fillRect(QRect(width() - 1, 0, 1, height()), t.hairline);
}

bool SidebarWidget::eventFilter(QObject* watched, QEvent* event) {
  if (tree_ && watched == tree_->viewport() &&
      event->type() == QEvent::MouseButtonPress) {
    auto* me = static_cast<QMouseEvent*>(event);
    if (me->button() == Qt::LeftButton &&
        !tree_->indexAt(me->pos()).isValid()) {
      clear_selection();
      // Not consumed: the view's own handling of a blank press is a no-op,
      // and swallowing it here would steal focus behaviour.
    }
  }
  return QWidget::eventFilter(watched, event);
}

void SidebarWidget::clear_selection() {
  if (!tree_ || !tree_->selectionModel()) return;
  // clearCurrentIndex fires currentChanged(invalid, …), which wipes
  // Node::selected via apply_selection and resets the Properties tab —
  // the exact path a row click takes, just toward "nothing".
  tree_->selectionModel()->clearCurrentIndex();
  tree_->clearSelection();
}

void SidebarWidget::clear() {
  model_->removeRows(0, model_->rowCount());
  scene_.reset();
  solo_node_     = scene::Node::kInvalid;
  selected_node_ = scene::Node::kInvalid;
  if (isolate_node_ != scene::Node::kInvalid) {
    isolate_node_ = scene::Node::kInvalid;
    emit isolate_changed(scene::Node::kInvalid);
  }
}

void SidebarWidget::set_scene(std::shared_ptr<scene::Scene> scene) {
  clear();
  scene_ = std::move(scene);
  if (scene_) {
    // The scene may carry viewer flags from the last time this document was
    // active; the tree selection and isolate state start fresh here, so the
    // flags must too. The shell re-applies a persisted isolate (per
    // document) via set_isolate right after handing over the scene.
    for (auto& n : scene_->nodes) {
      n.selected = false;
      n.ghosted  = false;
    }
  }
  rebuild();
}

void SidebarWidget::rebuild() {
  model_->removeRows(0, model_->rowCount());
  if (!scene_) return;

  auto build_item = [this](std::uint32_t idx) {
    const auto& node = scene_->nodes[idx];
    auto* item = new QStandardItem(QString::fromStdString(
      node.name.empty() ? std::string("(unnamed)") : node.name));
    item->setData(idx, kRoleNodeIndex);
    item->setData(node.visible, kRoleVisible);
    item->setData(node.ghosted, kRoleGhosted);
    item->setEditable(false);
    std::size_t tris = 0;
    if (node.mesh_index && *node.mesh_index < scene_->meshes.size() &&
        scene_->meshes[*node.mesh_index]) {
      tris = scene_->meshes[*node.mesh_index]->triangle_count();
    }
    if (tris > 0) {
      item->setData(QString::number(tris), kRoleTriCount);
    }
    return item;
  };

  std::function<void(QStandardItem*, std::uint32_t)> populate;
  populate = [&](QStandardItem* parent, std::uint32_t node_idx) {
    for (auto c : scene_->nodes[node_idx].children) {
      auto* item = build_item(c);
      parent->appendRow(item);
      populate(item, c);
    }
  };

  for (std::uint32_t i = 0; i < scene_->nodes.size(); ++i) {
    if (scene_->nodes[i].parent == scene::Node::kInvalid) {
      auto* item = build_item(i);
      model_->invisibleRootItem()->appendRow(item);
      populate(item, i);
    }
  }
  tree_->expandToDepth(1);
}

void SidebarWidget::set_subtree_visible(std::uint32_t node_index, bool visible) {
  if (!scene_ || node_index >= scene_->nodes.size()) return;
  scene_->nodes[node_index].visible = visible;
  for (auto c : scene_->nodes[node_index].children) {
    set_subtree_visible(c, visible);
  }
}

void SidebarWidget::set_subtree_selected(std::uint32_t node_index, bool selected) {
  if (!scene_ || node_index >= scene_->nodes.size()) return;
  scene_->nodes[node_index].selected = selected;
  for (auto c : scene_->nodes[node_index].children) {
    set_subtree_selected(c, selected);
  }
}

void SidebarWidget::set_subtree_ghosted(std::uint32_t node_index, bool ghosted) {
  if (!scene_ || node_index >= scene_->nodes.size()) return;
  scene_->nodes[node_index].ghosted = ghosted;
  for (auto c : scene_->nodes[node_index].children) {
    set_subtree_ghosted(c, ghosted);
  }
}

void SidebarWidget::sync_node_flags(QStandardItem* item) {
  if (!item) return;
  const auto v = item->data(kRoleNodeIndex);
  if (v.isValid() && scene_) {
    const auto idx = v.toUInt();
    if (idx < scene_->nodes.size()) {
      item->setData(scene_->nodes[idx].visible, kRoleVisible);
      item->setData(scene_->nodes[idx].ghosted, kRoleGhosted);
    }
  }
  for (int r = 0; r < item->rowCount(); ++r) sync_node_flags(item->child(r));
}

void SidebarWidget::apply_selection(std::uint32_t node_index) {
  selected_node_ = node_index;
  if (!scene_) return;
  for (auto& n : scene_->nodes) n.selected = false;
  // The isolate root is skipped on purpose: isolate shows that part
  // "normally", and painting the accent wash over the very thing the user
  // isolated would defeat the mode. Rows INSIDE the focus subtree (locating
  // a child of the isolated group) and ghosted rows outside it still
  // highlight, which is exactly the locate-a-part use case.
  if (node_index < scene_->nodes.size() && node_index != isolate_node_) {
    set_subtree_selected(node_index, true);
  }
  emit highlight_changed();
}

void SidebarWidget::select_node(std::uint32_t node_index) {
  if (!scene_ || node_index >= scene_->nodes.size()) return;
  std::function<QStandardItem*(QStandardItem*)> find =
    [&](QStandardItem* item) -> QStandardItem* {
      if (!item) return nullptr;
      const auto v = item->data(kRoleNodeIndex);
      if (v.isValid() && v.toUInt() == node_index) return item;
      for (int r = 0; r < item->rowCount(); ++r) {
        if (auto* hit = find(item->child(r))) return hit;
      }
      return nullptr;
    };
  auto* item = find(model_->invisibleRootItem());
  if (!item) return;
  const QModelIndex proxy_index = proxy_->mapFromSource(item->index());
  if (!proxy_index.isValid()) return;
  tree_->scrollTo(proxy_index);
  tree_->setCurrentIndex(proxy_index);  // currentChanged applies the flags
}

void SidebarWidget::set_isolate(std::uint32_t node_index) {
  if (!scene_ || node_index >= scene_->nodes.size()) {
    node_index = scene::Node::kInvalid;
  }
  isolate_node_ = node_index;
  if (scene_) {
    const bool active = node_index != scene::Node::kInvalid;
    for (auto& n : scene_->nodes) n.ghosted = active;
    if (active) {
      set_subtree_ghosted(node_index, false);
      // Ancestors stay un-ghosted, matching the solo rule: the containers
      // of the focus shouldn't read as "elsewhere" in tree or viewport.
      for (auto p = scene_->nodes[node_index].parent;
           p != scene::Node::kInvalid; p = scene_->nodes[p].parent) {
        scene_->nodes[p].ghosted = false;
      }
    }
  }
  // Selection tint depends on the isolate root (see apply_selection);
  // re-derive it for the current row before repainting.
  apply_selection(selected_node_);
  sync_node_flags(model_->invisibleRootItem());
  tree_->viewport()->update();
  emit isolate_changed(isolate_node_);
}

void SidebarWidget::toggle_eye(std::uint32_t node_index, bool solo) {
  if (!scene_ || node_index >= scene_->nodes.size()) return;

  if (solo) {
    if (solo_node_ == node_index) {
      // Second ⌥-click on the soloed node: bring everything back.
      for (auto& n : scene_->nodes) n.visible = true;
      solo_node_ = scene::Node::kInvalid;
    } else {
      for (auto& n : scene_->nodes) n.visible = false;
      set_subtree_visible(node_index, true);
      // Ancestors must stay visible too — the renderer draws per node, but
      // a hidden ancestor in the tree UI would look contradictory.
      for (auto p = scene_->nodes[node_index].parent;
           p != scene::Node::kInvalid; p = scene_->nodes[p].parent) {
        scene_->nodes[p].visible = true;
      }
      solo_node_ = node_index;
    }
  } else {
    solo_node_ = scene::Node::kInvalid;
    set_subtree_visible(node_index, !scene_->nodes[node_index].visible);
  }

  sync_node_flags(model_->invisibleRootItem());
  tree_->viewport()->update();
  emit visibility_changed();
}

void SidebarWidget::show_get_info(std::uint32_t node_index,
                                  const QRect& row_rect_global) {
  if (!scene_ || node_index >= scene_->nodes.size()) return;
  auto* card = new PropertiesPanel();
  card->set_scene(scene_);
  card->show_node(node_index);
  card->setMinimumWidth(260);
  const auto& n = scene_->nodes[node_index];
  Popover::show_at(card, row_rect_global,
                   QString::fromStdString(
                     n.name.empty() ? std::string("(unnamed)") : n.name),
                   /*pinnable=*/true, window());
}

} // namespace cadly::ui
