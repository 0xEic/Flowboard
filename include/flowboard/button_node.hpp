// SPDX-License-Identifier: MIT
#pragma once

namespace flowboard {

/// \file
/// \brief Interface for nodes that accept a button-press boolean from the control plane.

/// \brief Implemented by Debug.TriggerButton so the control plane can push a boolean
/// into a running graph when the user clicks the node's button in the web UI.
/// The control plane finds the node by id and dynamic_casts to this interface.
struct IButtonNode {
    virtual void button_press(bool value) = 0;

protected:
    ~IButtonNode() = default;
};

}  // namespace flowboard
