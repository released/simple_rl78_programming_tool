#include "m032_board_io.h"

#include <string.h>

#include "rl78_programmer/plp_contract.h"

static uint32_t ValidateOps(const M032BoardIoOps *ops)
{
    if (ops == 0 || ops->initialize == 0 || ops->set_tool0_low == 0 ||
        ops->set_uart_mode == 0 || ops->set_reset == 0 ||
        ops->set_target_power == 0 || ops->set_status_led == 0 ||
        ops->delay_us == 0)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    if (ops->target_vdd_sense_enabled > 1u ||
        (ops->target_vdd_sense_enabled != 0u &&
         ops->read_target_vdd_mv == 0))
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    return M032BoardProfile_Validate();
}

uint32_t M032BoardIo_Init(M032BoardIo *board, const M032BoardIoOps *ops)
{
    uint32_t status;
    if (board == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    status = ValidateOps(ops);
    if (status != PLP_OK)
    {
        return status;
    }
    memset(board, 0, sizeof(*board));
    memcpy(&board->ops, ops, sizeof(*ops));
    status = board->ops.initialize(board->ops.context);
    if (status == PLP_OK)
    {
        status = board->ops.set_tool0_low(board->ops.context, 0u);
    }
    if (status == PLP_OK)
    {
        status = board->ops.set_reset(board->ops.context, 0u);
    }
    if (status == PLP_OK)
    {
        status = board->ops.set_target_power(board->ops.context, 0u);
    }
    if (status == PLP_OK)
    {
        status = board->ops.set_status_led(board->ops.context, 0u);
    }
    if (status != PLP_OK)
    {
        return status;
    }
    board->initialized = 1u;
    return PLP_OK;
}

uint32_t M032BoardIo_SetStatusLed(M032BoardIo *board, uint8_t on)
{
    if (board == 0 || board->initialized == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    return board->ops.set_status_led(board->ops.context, on != 0u ? 1u : 0u);
}

uint32_t M032BoardIo_SetTargetPower(M032BoardIo *board, uint8_t enabled)
{
    if (board == 0 || board->initialized == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    return board->ops.set_target_power(board->ops.context, enabled != 0u ? 1u : 0u);
}

uint32_t M032BoardIo_ReadTargetVdd(M032BoardIo *board, uint16_t *millivolts)
{
    uint32_t status;
    uint16_t value;
    if (board == 0 || millivolts == 0 || board->initialized == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    if (board->ops.target_vdd_sense_enabled == 0u ||
        board->ops.read_target_vdd_mv == 0)
    {
        return PLP_ERR_CAPABILITY_UNSUPPORTED;
    }
    value = 0u;
    status = board->ops.read_target_vdd_mv(board->ops.context, &value);
    if (status != PLP_OK)
    {
        return status;
    }
    board->last_target_vdd_mv = value;
    *millivolts = value;
    return PLP_OK;
}

uint32_t M032BoardIo_LeaveSafe(M032BoardIo *board)
{
    uint32_t status;
    uint32_t next;
    if (board == 0 || board->initialized == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    status = board->ops.set_tool0_low(board->ops.context, 0u);
    next = board->ops.set_reset(board->ops.context, 0u);
    if (status == PLP_OK)
    {
        status = next;
    }
    board->programming_mode = 0u;
    return status;
}

uint32_t M032BoardIo_EnterProgrammingMode(M032BoardIo *board)
{
    uint32_t status;
    uint16_t vdd_mv;
    if (board == 0 || board->initialized == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    status = PLP_OK;
    if (board->ops.target_vdd_sense_enabled != 0u)
    {
        vdd_mv = 0u;
        status = M032BoardIo_ReadTargetVdd(board, &vdd_mv);
        if (status != PLP_OK)
        {
            return status;
        }
        if (vdd_mv < M032_BOARD_TARGET_VDD_MIN_MV ||
            vdd_mv > M032_BOARD_TARGET_VDD_MAX_MV)
        {
            return PLP_ERR_DRIVER_TARGET_STATUS;
        }
    }
    status = board->ops.set_reset(board->ops.context, 1u);
    if (status == PLP_OK)
    {
        status = board->ops.set_tool0_low(board->ops.context, 1u);
    }
    if (status == PLP_OK)
    {
        board->ops.delay_us(board->ops.context, M032_BOARD_TOOL0_SETUP_US);
        status = board->ops.set_reset(board->ops.context, 0u);
    }
    if (status == PLP_OK)
    {
        board->ops.delay_us(board->ops.context, M032_BOARD_TOOL0_HOLD_US);
        status = board->ops.set_uart_mode(board->ops.context, 0u);
    }
    if (status != PLP_OK)
    {
        (void)M032BoardIo_LeaveSafe(board);
        return status;
    }
    board->programming_mode = 1u;
    return PLP_OK;
}

uint32_t M032BoardIo_RestartTarget(M032BoardIo *board)
{
    uint32_t status;
    uint32_t next;
    if (board == 0 || board->initialized == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    status = board->ops.set_reset(board->ops.context, 1u);
    next = board->ops.set_tool0_low(board->ops.context, 0u);
    if (status == PLP_OK)
    {
        status = next;
    }
    if (status == PLP_OK)
    {
        board->ops.delay_us(board->ops.context, M032_BOARD_TOOL0_SETUP_US);
        status = board->ops.set_reset(board->ops.context, 0u);
    }
    board->programming_mode = 0u;
    return status;
}
