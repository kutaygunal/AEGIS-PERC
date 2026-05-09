#include "aegis/ui/violation_explorer_panel.hpp"

#include "aegis/ui/ui_state_text.hpp"
#include "aegis/ui/violation_filter_proxy_model.hpp"
#include "aegis/ui/violation_table_model.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QSplitter>
#include <QTableView>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include <nlohmann/json.hpp>

namespace aegis::ui {
namespace {

QString location_summary(const aegis::rules::ViolationLocation& location)
{
    QStringList parts;
    if (location.layer.has_value()) parts.append(QString("layer=%1").arg(QString::fromStdString(*location.layer)));
    if (location.net_name.has_value()) parts.append(QString("net=%1").arg(QString::fromStdString(*location.net_name)));
    if (location.device_name.has_value()) parts.append(QString("device=%1").arg(QString::fromStdString(*location.device_name)));
    if (location.pin_name.has_value()) parts.append(QString("pin=%1").arg(QString::fromStdString(*location.pin_name)));
    if (location.point.has_value()) parts.append(QString("point=(%1, %2)").arg(location.point->x, 0, 'f', 2).arg(location.point->y, 0, 'f', 2));
    return parts.isEmpty() ? QString("No resolved location") : parts.join(" | ");
}

QString property_value_to_string(const aegis::graph::PropertyValue& value)
{
    return std::visit([](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::string>) return QString::fromStdString(v);
        else if constexpr (std::is_same_v<T, bool>) return QString(v ? "true" : "false");
        else return QString::number(v);
    }, value);
}

std::optional<aegis::rules::Severity> severity_from_combo_text(const QString& text)
{
    if (text.trimmed().isEmpty() || text == "All") {
        return std::nullopt;
    }
    return aegis::rules::severity_from_string(text.toStdString());
}

} // namespace

ViolationExplorerPanel::ViolationExplorerPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    auto* filter_row_1 = new QHBoxLayout();
    m_severity_filter = new QComboBox(this);
    m_severity_filter->setAccessibleName("Violation Severity Filter");
    m_severity_filter->setToolTip("Filter visible violations by severity");
    m_severity_filter->addItems({"All", "info", "warning", "error", "fatal"});
    m_rule_filter = new QLineEdit(this);
    m_rule_filter->setAccessibleName("Violation Rule Filter");
    m_rule_filter->setToolTip("Filter visible violations by rule ID");
    m_rule_filter->setPlaceholderText("Rule ID");
    m_layer_filter = new QLineEdit(this);
    m_layer_filter->setAccessibleName("Violation Layer Filter");
    m_layer_filter->setToolTip("Filter visible violations by layer name");
    m_layer_filter->setPlaceholderText("Layer");
    m_net_filter = new QLineEdit(this);
    m_net_filter->setAccessibleName("Violation Net Filter");
    m_net_filter->setToolTip("Filter visible violations by net name");
    m_net_filter->setPlaceholderText("Net");
    filter_row_1->addWidget(new QLabel("Severity:", this));
    filter_row_1->addWidget(m_severity_filter);
    filter_row_1->addWidget(m_rule_filter);
    filter_row_1->addWidget(m_layer_filter);
    filter_row_1->addWidget(m_net_filter);
    root->addLayout(filter_row_1);

    auto* filter_row_2 = new QHBoxLayout();
    m_search_filter = new QLineEdit(this);
    m_search_filter->setAccessibleName("Violation Search Filter");
    m_search_filter->setToolTip("Search visible violations by free text");
    m_search_filter->setPlaceholderText("Search text");
    auto* clear_button = new QPushButton("&Clear Filters", this);
    clear_button->setAccessibleName("Clear Violation Filters");
    clear_button->setToolTip("Clear all violation filters");
    m_filter_summary = new QLabel("0 / 0 violations", this);
    filter_row_2->addWidget(m_search_filter, 1);
    filter_row_2->addWidget(clear_button);
    filter_row_2->addWidget(m_filter_summary);
    root->addLayout(filter_row_2);

    auto* heatmap_row = new QHBoxLayout();
    m_heatmap_visible = new QCheckBox("&Heatmap", this);
    m_heatmap_visible->setAccessibleName("Violation Heatmap Toggle");
    m_heatmap_visible->setToolTip("Show or hide the violation heatmap overlay");
    m_heatmap_opacity = new QSlider(Qt::Horizontal, this);
    m_heatmap_opacity->setAccessibleName("Violation Heatmap Opacity");
    m_heatmap_opacity->setToolTip("Adjust the violation heatmap opacity");
    m_heatmap_opacity->setRange(0, 100);
    m_heatmap_opacity->setValue(60);
    heatmap_row->addWidget(m_heatmap_visible);
    heatmap_row->addWidget(new QLabel("Opacity:", this));
    heatmap_row->addWidget(m_heatmap_opacity, 1);
    root->addLayout(heatmap_row);

    auto* splitter = new QSplitter(Qt::Vertical, this);
    root->addWidget(splitter);

    m_model = new ViolationTableModel(this);
    m_proxy_model = new ViolationFilterProxyModel(this);
    m_proxy_model->setSourceModel(m_model);

    m_table = new QTableView(this);
    m_table->setAccessibleName("Violations Table");
    m_table->setToolTip("Visible violations with keyboard and context-menu actions");
    m_table->setModel(m_proxy_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    splitter->addWidget(m_table);

    auto* details = new QWidget(this);
    auto* details_layout = new QVBoxLayout(details);
    details_layout->setContentsMargins(0, 0, 0, 0);

    auto* id_layout = new QHBoxLayout();
    id_layout->addWidget(new QLabel("Violation ID:", details));
    m_violation_id = new QLineEdit(details);
    m_violation_id->setAccessibleName("Current Violation ID");
    m_violation_id->setToolTip("Stable identifier for the selected violation");
    m_violation_id->setReadOnly(true);
    id_layout->addWidget(m_violation_id);
    details_layout->addLayout(id_layout);

    details_layout->addWidget(new QLabel("Details:", details));
    m_summary = new QTextEdit(details);
    m_summary->setAccessibleName("Violation Details");
    m_summary->setToolTip("Details for the selected violation");
    m_summary->setReadOnly(true);
    m_summary->setMinimumHeight(100);
    details_layout->addWidget(m_summary);

    details_layout->addWidget(new QLabel("Metadata:", details));
    m_metadata = new QTableWidget(0, 2, details);
    m_metadata->setAccessibleName("Violation Metadata");
    m_metadata->setToolTip("Metadata for the selected violation");
    m_metadata->setHorizontalHeaderLabels({"Key", "Value"});
    m_metadata->horizontalHeader()->setStretchLastSection(true);
    m_metadata->verticalHeader()->setVisible(false);
    m_metadata->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_metadata->setSelectionMode(QAbstractItemView::NoSelection);
    details_layout->addWidget(m_metadata);

    splitter->addWidget(details);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    m_trace_action = new QAction("Trace from Violation", this);
    m_trace_action->setToolTip("Trace connectivity from the selected violation");
    m_center_action = new QAction("Center on Canvas", this);
    m_center_action->setToolTip("Center the layout canvas on the selected violation");
    m_copy_id_action = new QAction("Copy Violation ID", this);
    m_copy_id_action->setToolTip("Copy the selected violation ID");
    m_copy_details_action = new QAction("Copy Violation Details", this);
    m_copy_details_action->setToolTip("Copy the selected violation details");
    m_navigate_related_action = new QAction("Open Related Graph/Import Data", this);
    m_navigate_related_action->setToolTip("Open related graph or imported artifact context for the selected violation");
    m_copy_selected_action = new QAction("Copy Selected Violations", this);
    m_copy_selected_action->setToolTip("Copy all selected violations");
    m_export_selected_action = new QAction("Export Selected Violations", this);
    m_export_selected_action->setToolTip("Export all selected violations as JSON");

    connect(m_trace_action, &QAction::triggered, this, &ViolationExplorerPanel::trace_current_violation_requested);
    connect(m_center_action, &QAction::triggered, this, &ViolationExplorerPanel::center_current_violation_requested);
    connect(m_copy_id_action, &QAction::triggered, this, &ViolationExplorerPanel::copy_current_violation_id_requested);
    connect(m_copy_details_action, &QAction::triggered, this, &ViolationExplorerPanel::copy_current_violation_details_requested);
    connect(m_navigate_related_action, &QAction::triggered, this, &ViolationExplorerPanel::navigate_current_violation_requested);
    connect(m_copy_selected_action, &QAction::triggered, this, &ViolationExplorerPanel::copy_selected_violations_requested);
    connect(m_export_selected_action, &QAction::triggered, this, &ViolationExplorerPanel::export_selected_violations_requested);

    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        update_context_action_state();
        QMenu menu(this);
        menu.addAction(m_trace_action);
        menu.addAction(m_center_action);
        menu.addSeparator();
        menu.addAction(m_copy_id_action);
        menu.addAction(m_copy_details_action);
        menu.addAction(m_navigate_related_action);
        menu.addSeparator();
        menu.addAction(m_copy_selected_action);
        menu.addAction(m_export_selected_action);
        menu.exec(m_table->viewport()->mapToGlobal(pos));
    });

    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex&, const QModelIndex&) {
                update_details();
                update_context_action_state();
                emit current_violation_changed();
            });

    auto connect_filter = [this]() {
        apply_filters_from_widgets();
    };
    connect(m_severity_filter, &QComboBox::currentTextChanged, this, [connect_filter](const QString&) { connect_filter(); });
    connect(m_rule_filter, &QLineEdit::textChanged, this, [connect_filter](const QString&) { connect_filter(); });
    connect(m_layer_filter, &QLineEdit::textChanged, this, [connect_filter](const QString&) { connect_filter(); });
    connect(m_net_filter, &QLineEdit::textChanged, this, [connect_filter](const QString&) { connect_filter(); });
    connect(m_search_filter, &QLineEdit::textChanged, this, [connect_filter](const QString&) { connect_filter(); });
    connect(clear_button, &QPushButton::clicked, this, &ViolationExplorerPanel::clear_filters);
    const auto emit_heatmap = [this]() {
        emit heatmap_settings_changed(heatmap_visible(), heatmap_opacity());
    };
    connect(m_heatmap_visible, &QCheckBox::toggled, this, [emit_heatmap](bool) { emit_heatmap(); });
    connect(m_heatmap_opacity, &QSlider::valueChanged, this, [emit_heatmap](int) { emit_heatmap(); });

    QWidget::setTabOrder(m_severity_filter, m_rule_filter);
    QWidget::setTabOrder(m_rule_filter, m_layer_filter);
    QWidget::setTabOrder(m_layer_filter, m_net_filter);
    QWidget::setTabOrder(m_net_filter, m_search_filter);
    QWidget::setTabOrder(m_search_filter, clear_button);
    QWidget::setTabOrder(clear_button, m_heatmap_visible);
    QWidget::setTabOrder(m_heatmap_visible, m_heatmap_opacity);
    QWidget::setTabOrder(m_heatmap_opacity, m_table);
    QWidget::setTabOrder(m_table, m_violation_id);
    QWidget::setTabOrder(m_violation_id, m_summary);
    QWidget::setTabOrder(m_summary, m_metadata);

    update_filter_summary();
    update_details();
    update_context_action_state();
}

void ViolationExplorerPanel::set_violations(aegis::rules::ViolationCollection violations)
{
    m_violations = std::move(violations);
    m_model->set_violations(m_violations);
    update_filter_summary();
    emit filtered_violations_changed(filtered_violations());
    if (violation_count() > 0) {
        select_row(0);
    } else {
        update_details();
        update_context_action_state();
        emit current_violation_changed();
    }
}

int ViolationExplorerPanel::violation_count() const noexcept
{
    return m_proxy_model->rowCount();
}

int ViolationExplorerPanel::total_violation_count() const noexcept
{
    return m_model->violation_count();
}

const aegis::rules::Violation* ViolationExplorerPanel::current_violation() const
{
    const QModelIndex index = m_table->currentIndex();
    return m_proxy_model->violation_at_proxy_row(index.row());
}

void ViolationExplorerPanel::select_row(int row)
{
    if (row < 0 || row >= violation_count()) {
        m_table->clearSelection();
        m_table->setCurrentIndex(QModelIndex{});
        update_details();
        emit current_violation_changed();
        return;
    }

    const QModelIndex index = m_proxy_model->index(row, 0);
    m_table->setCurrentIndex(index);
    m_table->selectRow(row);
}

void ViolationExplorerPanel::set_filter_state(ViolationFilterState state)
{
    apply_filter_widgets(state);
    m_proxy_model->set_filter_state(state);
    update_filter_summary();
    emit filtered_violations_changed(filtered_violations());
    if (violation_count() > 0) {
        select_row(0);
    } else {
        select_row(-1);
    }
    update_context_action_state();
}

ViolationFilterState ViolationExplorerPanel::filter_state() const
{
    return m_proxy_model->filter_state();
}

void ViolationExplorerPanel::clear_filters()
{
    set_filter_state({});
}

aegis::rules::ViolationCollection ViolationExplorerPanel::filtered_violations() const
{
    std::vector<aegis::rules::Violation> filtered;
    filtered.reserve(static_cast<std::size_t>(violation_count()));
    for (int row = 0; row < violation_count(); ++row) {
        const auto* violation = m_proxy_model->violation_at_proxy_row(row);
        if (violation != nullptr) {
            filtered.push_back(*violation);
        }
    }
    return aegis::rules::ViolationCollection{std::move(filtered)};
}

QString ViolationExplorerPanel::filter_summary_text() const
{
    return m_filter_summary->text();
}

void ViolationExplorerPanel::refresh_preview_state()
{
    update_details();
    update_context_action_state();
    emit filtered_violations_changed(filtered_violations());
    emit current_violation_changed();
}

QString ViolationExplorerPanel::current_violation_id() const
{
    return m_violation_id->text();
}

QString ViolationExplorerPanel::details_summary_text() const
{
    return m_summary->toPlainText();
}

int ViolationExplorerPanel::metadata_row_count() const
{
    return m_metadata->rowCount();
}

int ViolationExplorerPanel::selected_violation_count() const
{
    return m_table != nullptr && m_table->selectionModel() != nullptr
        ? m_table->selectionModel()->selectedRows().size()
        : 0;
}

void ViolationExplorerPanel::select_rows(const std::vector<int>& rows)
{
    if (m_table == nullptr || m_table->selectionModel() == nullptr) {
        return;
    }
    m_table->clearSelection();
    QModelIndex current_index;
    if (rows.empty()) {
        m_table->setCurrentIndex(QModelIndex{});
    }
    for (const int row : rows) {
        if (row < 0 || row >= violation_count()) {
            continue;
        }
        const QModelIndex index = m_proxy_model->index(row, 0);
        m_table->selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        current_index = index;
    }
    if (current_index.isValid()) {
        m_table->selectionModel()->setCurrentIndex(current_index, QItemSelectionModel::NoUpdate);
    }
    update_details();
    update_context_action_state();
    emit current_violation_changed();
}

int ViolationExplorerPanel::select_violation_ids(const QStringList& ids)
{
    if (ids.isEmpty()) {
        select_rows({});
        return 0;
    }

    std::vector<int> rows;
    for (int row = 0; row < violation_count(); ++row) {
        if (const auto* violation = m_proxy_model->violation_at_proxy_row(row); violation != nullptr
            && ids.contains(QString::fromStdString(violation->id))) {
            rows.push_back(row);
        }
    }
    select_rows(rows);
    return static_cast<int>(rows.size());
}

QString ViolationExplorerPanel::selected_violations_text() const
{
    QStringList lines;
    if (m_table == nullptr || m_table->selectionModel() == nullptr) {
        return {};
    }
    for (const auto& index : m_table->selectionModel()->selectedRows()) {
        if (const auto* violation = m_proxy_model->violation_at_proxy_row(index.row()); violation != nullptr) {
            lines.append(QString("%1 | %2 | %3")
                             .arg(QString::fromStdString(violation->id))
                             .arg(QString::fromStdString(violation->rule_id))
                             .arg(QString::fromStdString(violation->message)));
        }
    }
    return lines.join('\n');
}

QString ViolationExplorerPanel::selected_violations_json_text() const
{
    nlohmann::json out = nlohmann::json::array();
    if (m_table == nullptr || m_table->selectionModel() == nullptr) {
        return QString::fromStdString(out.dump(2));
    }
    for (const auto& index : m_table->selectionModel()->selectedRows()) {
        if (const auto* violation = m_proxy_model->violation_at_proxy_row(index.row()); violation != nullptr) {
            out.push_back(*violation);
        }
    }
    return QString::fromStdString(out.dump(2));
}

QString ViolationExplorerPanel::current_violation_details_for_copy() const
{
    return details_summary_text();
}

QString ViolationExplorerPanel::current_violation_id_for_copy() const
{
    return current_violation_id();
}

bool ViolationExplorerPanel::context_action_enabled(const QString& action_id) const
{
    if (action_id == "trace") return m_trace_action != nullptr && m_trace_action->isEnabled();
    if (action_id == "center") return m_center_action != nullptr && m_center_action->isEnabled();
    if (action_id == "copy_id") return m_copy_id_action != nullptr && m_copy_id_action->isEnabled();
    if (action_id == "copy_details") return m_copy_details_action != nullptr && m_copy_details_action->isEnabled();
    if (action_id == "navigate_related") return m_navigate_related_action != nullptr && m_navigate_related_action->isEnabled();
    if (action_id == "copy_selected") return m_copy_selected_action != nullptr && m_copy_selected_action->isEnabled();
    if (action_id == "export_selected") return m_export_selected_action != nullptr && m_export_selected_action->isEnabled();
    return false;
}

bool ViolationExplorerPanel::trigger_context_action(const QString& action_id)
{
    if (!context_action_enabled(action_id)) {
        return false;
    }
    if (action_id == "trace") m_trace_action->trigger();
    else if (action_id == "center") m_center_action->trigger();
    else if (action_id == "copy_id") m_copy_id_action->trigger();
    else if (action_id == "copy_details") m_copy_details_action->trigger();
    else if (action_id == "navigate_related") m_navigate_related_action->trigger();
    else if (action_id == "copy_selected") m_copy_selected_action->trigger();
    else if (action_id == "export_selected") m_export_selected_action->trigger();
    else return false;
    return true;
}

void ViolationExplorerPanel::set_heatmap_visible(bool visible)
{
    if (m_heatmap_visible != nullptr) {
        m_heatmap_visible->setChecked(visible);
    }
}

void ViolationExplorerPanel::set_heatmap_opacity(double opacity)
{
    if (m_heatmap_opacity != nullptr) {
        const int slider_value = std::clamp(static_cast<int>(std::lround(opacity * 100.0)), 0, 100);
        m_heatmap_opacity->setValue(slider_value);
    }
}

bool ViolationExplorerPanel::heatmap_visible() const
{
    return m_heatmap_visible != nullptr && m_heatmap_visible->isChecked();
}

double ViolationExplorerPanel::heatmap_opacity() const
{
    return m_heatmap_opacity != nullptr ? static_cast<double>(m_heatmap_opacity->value()) / 100.0 : 0.6;
}

void ViolationExplorerPanel::apply_filter_widgets(const ViolationFilterState& state)
{
    m_severity_filter->setCurrentText(state.severity.has_value()
        ? QString::fromStdString(aegis::rules::severity_to_string(*state.severity))
        : QString("All"));
    m_rule_filter->setText(QString::fromStdString(state.rule_id));
    m_layer_filter->setText(QString::fromStdString(state.layer));
    m_net_filter->setText(QString::fromStdString(state.net));
    m_search_filter->setText(QString::fromStdString(state.search_text));
}

void ViolationExplorerPanel::apply_filters_from_widgets()
{
    ViolationFilterState state;
    state.severity = severity_from_combo_text(m_severity_filter->currentText());
    state.rule_id = m_rule_filter->text().trimmed().toStdString();
    state.layer = m_layer_filter->text().trimmed().toStdString();
    state.net = m_net_filter->text().trimmed().toStdString();
    state.search_text = m_search_filter->text().trimmed().toStdString();

    m_proxy_model->set_filter_state(state);
    update_filter_summary();
    emit filtered_violations_changed(filtered_violations());
    if (violation_count() > 0) {
        select_row(0);
    } else {
        select_row(-1);
    }
}

void ViolationExplorerPanel::update_filter_summary()
{
    m_filter_summary->setText(QString("%1 / %2 violations").arg(violation_count()).arg(total_violation_count()));
}

void ViolationExplorerPanel::update_context_action_state()
{
    const bool has_current = current_violation() != nullptr;
    const bool has_selection = selected_violation_count() > 0;
    if (m_trace_action != nullptr) m_trace_action->setEnabled(has_current);
    if (m_center_action != nullptr) m_center_action->setEnabled(has_current);
    if (m_copy_id_action != nullptr) m_copy_id_action->setEnabled(has_current);
    if (m_copy_details_action != nullptr) m_copy_details_action->setEnabled(has_current);
    if (m_navigate_related_action != nullptr) m_navigate_related_action->setEnabled(has_current);
    if (m_copy_selected_action != nullptr) m_copy_selected_action->setEnabled(has_selection);
    if (m_export_selected_action != nullptr) m_export_selected_action->setEnabled(has_selection);
}

void ViolationExplorerPanel::update_details()
{
    const auto* violation = current_violation();
    m_metadata->setRowCount(0);

    if (violation == nullptr) {
        m_violation_id->clear();
        if (total_violation_count() == 0) {
            m_summary->setPlainText(state_text::violations_empty());
        } else if (violation_count() == 0) {
            m_summary->setPlainText(state_text::violations_filtered_empty());
        } else {
            m_summary->setPlainText(state_text::violations_no_selection());
        }
        update_context_action_state();
        return;
    }

    m_violation_id->setText(QString::fromStdString(violation->id));
    m_summary->setPlainText(
        QString("Rule: %1\nSeverity: %2\nMessage: %3\nLocation: %4")
            .arg(QString::fromStdString(violation->rule_id))
            .arg(QString::fromStdString(aegis::rules::severity_to_string(violation->severity)))
            .arg(QString::fromStdString(violation->message))
            .arg(location_summary(violation->location)));

    int row = 0;
    for (const auto& [key, value] : violation->metadata) {
        m_metadata->insertRow(row);
        m_metadata->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(key)));
        m_metadata->setItem(row, 1, new QTableWidgetItem(property_value_to_string(value)));
        ++row;
    }
    update_context_action_state();
}

} // namespace aegis::ui
