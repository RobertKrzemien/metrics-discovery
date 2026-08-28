/*========================== begin_copyright_notice ============================

Copyright (C) 2026 Intel Corporation

SPDX-License-Identifier: MIT

============================= end_copyright_notice ===========================*/

//     File Name:  md_euss_concurrent_group.cpp

//     Abstract:   C++ Metrics Discovery euss concurrent group implementation

#include "md_adapter.h"
#include "md_metrics_device.h"
#include "md_euss_concurrent_group.h"
#include "md_metric_set.h"
#include "md_information.h"
#include "md_driver_ifc.h"
#include "md_utils.h"

namespace MetricsDiscoveryInternal
{
    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CEUSSConcurrentGroup
    //
    // Method:
    //     CEUSSConcurrentGroup constructor
    //
    // Description:
    //     Constructor.
    //
    // Input:
    //     CMetricsDevice& device                 - parent metrics device
    //     const char*     name                   - concurrent group name
    //     const char*     description            - concurrent group description
    //     const uint32_t  measurementTypeMask    - measurement type mask
    //
    //////////////////////////////////////////////////////////////////////////////
    CEUSSConcurrentGroup::CEUSSConcurrentGroup( CMetricsDevice& device, const char* name, const char* description, const uint32_t measurementTypeMask )
        : CConcurrentGroup( device, name, description, measurementTypeMask )
        , m_gpuMaxFrequency( m_device.GetGlobalSymbolValueByName( "GpuMaxFrequencyMHz" ) )
        , m_metricSet( nullptr )
        , m_bufferOverflowInfo( nullptr )
        , m_bufferSize( 0 )
        , m_streamId( -1 )
    {
        m_bufferOverflowInfo = new( std::nothrow ) CInformation( m_device, 0, "BufferOverflow", "Buffer Overflow", "The flag indicating that some reports have been overwritten.", "Report Meta Data", API_TYPE_IOSTREAM, INFORMATION_TYPE_FLAG, nullptr );

        if( m_bufferOverflowInfo &&
            m_bufferOverflowInfo->SetSnapshotReportReadEquation( "0" ) == CC_OK &&
            m_bufferOverflowInfo->SetDeltaReportReadEquation( "0" ) == CC_OK )
        {
            m_params.IoMeasurementInformationCount = 1;
        }
        else
        {
            MD_SAFE_DELETE( m_bufferOverflowInfo );
        }
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CEUSSConcurrentGroup
    //
    // Method:
    //     OpenIoStream
    //
    // Description:
    //     Open EUSS Stream for given metric set.
    //
    // Input:
    //     IMetricSet_1_0*      metricSet           - metric set
    //     uint32_t             processId           - PID of the measured app
    //     uint32_t*            nsTimerPeriod       - (in/out) requested/set sampling period time in nanoseconds
    //     uint32_t*            eussBufferSize      - (in/out) requested/set EUSS Buffer size in bytes
    //
    // Output:
    //     TCompletionCode                          - result of operation (*CC_OK* is OK)
    //
    //////////////////////////////////////////////////////////////////////////////
    TCompletionCode CEUSSConcurrentGroup::OpenIoStream( IMetricSet_1_0* metricSet, uint32_t processId, uint32_t* nsTimerPeriod, uint32_t* eussBufferSize )
    {
        return OpenIoStream( static_cast<IMetricSet_1_13*>( metricSet ), processId, nsTimerPeriod, eussBufferSize, IO_STREAM_STATE_ENABLED );
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CEUSSConcurrentGroup
    //
    // Method:
    //     ReadIoStream
    //
    // Description:
    //     Reads data from previously opened EUSS Stream.
    //
    // Input:
    //     uint32_t*       reportCount - (in/out) requested number of reports to read / reports read from the stream
    //     char*           reportData  - (in/out) pointer to the read data
    //     uint32_t        readFlags   - read flags (see TIoReadFlag enum), 0 is ok
    //
    // Output:
    //     TCompletionCode             - result of operation (*CC_OK* is ok)
    //
    //////////////////////////////////////////////////////////////////////////////
    TCompletionCode CEUSSConcurrentGroup::ReadIoStream( uint32_t* reportCount, char* reportData, [[maybe_unused]] uint32_t readFlags )
    {
        const uint32_t adapterId = m_device.GetAdapter().GetAdapterId();
        MD_LOG_ENTER_A( adapterId );

        MD_CHECK_PTR_RET_A( adapterId, reportData, CC_ERROR_INVALID_PARAMETER );
        MD_CHECK_PTR_RET_A( adapterId, reportCount, CC_ERROR_INVALID_PARAMETER );

        if( m_metricSet == nullptr )
        {
            *reportCount = 0;
            MD_LOG_A( adapterId, LOG_ERROR, "stream not opened" );
            return CC_ERROR_GENERAL;
        }

        const uint32_t reportSize     = m_metricSet->GetParams()->RawReportSize;
        const uint32_t bytesToRead    = *reportCount * reportSize;
        bool           bufferOverflow = false;
        uint32_t       readBytes      = 0;

        CDriverInterface& driverInterface = m_device.GetDriverInterface();
        TCompletionCode   ret             = driverInterface.ReadEussStream( *this, reportSize, *reportCount, reportData, readBytes, bufferOverflow );
        MD_CHECK_CC_RET_A( adapterId, ret );

        if( m_bufferOverflowInfo )
        {
            m_bufferOverflowInfo->SetInformationValue( bufferOverflow ? 1 : 0, EQUATION_IO_READ );
        }

        *reportCount = readBytes / reportSize;

        MD_LOG_EXIT_A( adapterId );
        return ( readBytes < bytesToRead ) ? CC_READ_PENDING : CC_OK;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CEUSSConcurrentGroup
    //
    // Method:
    //     CloseIoStream
    //
    // Description:
    //     Closes previously opened EUSS Stream.
    //
    // Output:
    //     TCompletionCode - result of operation (*CC_OK* is ok)
    //
    //////////////////////////////////////////////////////////////////////////////
    TCompletionCode CEUSSConcurrentGroup::CloseIoStream( void )
    {
        TCompletionCode ret       = CC_ERROR_GENERAL;
        const uint32_t  adapterId = m_device.GetAdapter().GetAdapterId();
        MD_LOG_ENTER_A( adapterId );

        if( m_metricSet != nullptr )
        {
            CDriverInterface& driverInterface = m_device.GetDriverInterface();

            ret = driverInterface.CloseEussStream( *this );
            MD_CHECK_CC_RET_A( adapterId, ret );

            // nullptr indicates stream is closed
            m_metricSet = nullptr;
        }
        else
        {
            MD_LOG_A( adapterId, LOG_ERROR, "stream not opened" );
        }

        MD_LOG_EXIT_A( adapterId );
        return ret;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CEUSSConcurrentGroup
    //
    // Method:
    //     WaitForReports
    //
    // Description:
    //     Waits the given number of milliseconds for reports from EUSS Stream.
    //     Returns *CC_OK* if wait was successful (data waiting in the buffer was signaled).
    //
    // Input:
    //     uint32_t milliseconds - number of milliseconds to wait
    //
    // Output:
    //     TCompletionCode       - result of the operation
    //
    //////////////////////////////////////////////////////////////////////////////
    TCompletionCode CEUSSConcurrentGroup::WaitForReports( uint32_t milliseconds )
    {
        if( m_metricSet == nullptr )
        {
            MD_LOG_A( m_device.GetAdapter().GetAdapterId(), LOG_ERROR, "stream not opened" );
            return CC_ERROR_GENERAL;
        }

        auto& driverInterface = m_device.GetDriverInterface();

        return driverInterface.WaitForEussStreamReports( *this, milliseconds, m_metricSet->GetParams()->RawReportSize );
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CEUSSConcurrentGroup
    //
    // Method:
    //     OpenIoStream
    //
    // Description:
    //     Open EUSS Stream for given metric set.
    //
    // Input:
    //     IMetricSet_1_13* metricSet           - metric set
    //     uint32_t         processId           - PID of the measured app
    //     uint32_t*        nsTimerPeriod       - (in/out) requested/set sampling period time in nanoseconds
    //     uint32_t*        eussBufferSize      - (in/out) requested/set EUSS Buffer size in bytes
    //     TIoStreamState   state               - IO Stream state (enabled/disabled)
    //
    // Output:
    //     TCompletionCode                      - result of operation (*CC_OK* is OK)
    //
    //////////////////////////////////////////////////////////////////////////////
    TCompletionCode CEUSSConcurrentGroup::OpenIoStream( IMetricSet_1_13* metricSet, uint32_t processId, uint32_t* nsTimerPeriod, uint32_t* eussBufferSize, TIoStreamState state )
    {
        const uint32_t adapterId = m_device.GetAdapter().GetAdapterId();
        MD_LOG_ENTER_A( adapterId );

        if( processId != 0 || !m_gpuMaxFrequency || m_gpuMaxFrequency->ValueUInt32 == 0 )
        {
            return CC_ERROR_NOT_SUPPORTED;
        }

        // Close previous stream if opened
        if( m_metricSet )
        {
            CloseIoStream();
        }

        MD_CHECK_PTR_RET_A( adapterId, metricSet, CC_ERROR_INVALID_PARAMETER );
        MD_CHECK_PTR_RET_A( adapterId, nsTimerPeriod, CC_ERROR_INVALID_PARAMETER );
        MD_CHECK_PTR_RET_A( adapterId, eussBufferSize, CC_ERROR_INVALID_PARAMETER );

        auto metricSetInternal = static_cast<CMetricSet*>( metricSet );
        if( metricSetInternal->GetConcurrentGroup() != this )
        {
            MD_LOG_A( adapterId, LOG_ERROR, "Error: Given metric set belongs to another concurrent group" );
            return CC_ERROR_INVALID_PARAMETER;
        }

        // Convert ns to sample rate (coefficient of EU_STALL_SAMPLING_TICKS)
        uint32_t sampleRate = TicksToSampleRate( NsToTicks( *nsTimerPeriod ) );

        TIoStreamState    defaultState    = IO_STREAM_STATE_DISABLED;
        CDriverInterface& driverInterface = m_device.GetDriverInterface();
        TCompletionCode   ret             = driverInterface.OpenEussStream( *this, sampleRate, *eussBufferSize, defaultState );
        MD_CHECK_CC_RET_A( adapterId, ret );

        // Default state is different than requested state, change it to requested state
        if( defaultState != state )
        {
            ret = driverInterface.ChangeEussStreamState( *this, state, sampleRate );

            if( ret != CC_OK )
            {
                // Close the stream if changing state failed.
                TCompletionCode ret2 = driverInterface.CloseEussStream( *this );
                MD_CHECK_CC_RET_A( adapterId, ret2 );
                MD_LOG_EXIT_A( adapterId );
                return ret;
            }

            MD_LOG_A( adapterId, LOG_DEBUG, "Stream state changed to: %u", state );
        }

        // Convert rounded ticks back to ns
        *nsTimerPeriod = TicksToNs( sampleRate * EU_STALL_SAMPLING_TICKS );

        // Get number of XeCores to calculate buffer size
        TTypedValue_1_0* xeCoreTotalCount = m_device.GetGlobalSymbolValueByName( "XeCoreTotalCount" );
        *eussBufferSize                   = EU_STALL_SAMPLING_MAX_BUFFER_SIZE * ( xeCoreTotalCount ? xeCoreTotalCount->ValueUInt32 : 0 );

        m_metricSet  = metricSetInternal;
        m_bufferSize = *eussBufferSize;

        MD_LOG_EXIT_A( adapterId );
        return ret;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CEUSSConcurrentGroup
    //
    // Method:
    //     ChangeIoStreamState
    //
    // Description:
    //     Changes EUSS Stream state (enabled/disabled).
    //
    // Input:
    //     TIoStreamState   state               - IO Stream state (enabled/disabled)
    //     uint32_t*        nsTimerPeriod       - (in/out) requested/set sampling period time in nanoseconds
    //
    // Output:
    //     TCompletionCode                      - result of operation (*CC_OK* is OK)
    //
    //////////////////////////////////////////////////////////////////////////////
    TCompletionCode CEUSSConcurrentGroup::ChangeIoStreamState( TIoStreamState state, uint32_t* nsTimerPeriod )
    {
        const uint32_t adapterId = m_device.GetAdapter().GetAdapterId();
        MD_LOG_ENTER_A( adapterId );
        MD_CHECK_PTR_RET_A( adapterId, nsTimerPeriod, CC_ERROR_INVALID_PARAMETER );

        if( !m_metricSet )
        {
            MD_LOG_EXIT_A( adapterId );
            return CC_ERROR_NOT_SUPPORTED;
        }

        MD_LOG_A( adapterId, LOG_DEBUG, "Changing stream state to: %u, timer period to: %u ns", state, *nsTimerPeriod );

        // Convert ns to sample rate (coefficient of EU_STALL_SAMPLING_TICKS)
        uint32_t          sampleRate      = TicksToSampleRate( NsToTicks( *nsTimerPeriod ) );
        CDriverInterface& driverInterface = m_device.GetDriverInterface();
        TCompletionCode   ret             = driverInterface.ChangeEussStreamState( *this, state, sampleRate );
        MD_CHECK_CC_RET_A( adapterId, ret );

        // Convert rounded ticks back to ns
        *nsTimerPeriod = TicksToNs( sampleRate * EU_STALL_SAMPLING_TICKS );

        MD_LOG_A( adapterId, LOG_DEBUG, "Stream state changed to: %u, timer period to: %u ns", state, *nsTimerPeriod );

        MD_LOG_EXIT_A( adapterId );
        return ret;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CEUSSConcurrentGroup
    //
    // Method:
    //     GetIoMeasurementInformation
    //
    // Description:
    //     Returns measurement information of given index. Measurement information values
    //     are set after successful ReadIoStream.
    //
    // Input:
    //     uint32_t          index - index of the measurement information
    //
    // Output:
    //     IInformation_1_0*       - measurement information, nullptr if count is 0
    //
    //////////////////////////////////////////////////////////////////////////////
    IInformation_1_0* CEUSSConcurrentGroup::GetIoMeasurementInformation( uint32_t index )
    {
        return ( index < m_params.IoMeasurementInformationCount )
            ? static_cast<IInformation_1_0*>( m_bufferOverflowInfo )
            : nullptr;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CEUSSConcurrentGroup
    //
    // Method:
    //     GetStreamId
    //
    // Description:
    //     Returns stream id.
    //
    // Output:
    //     int32_t - stream id.
    //
    //////////////////////////////////////////////////////////////////////////////
    int32_t CEUSSConcurrentGroup::GetStreamId() const
    {
        return m_streamId;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CEUSSConcurrentGroup
    //
    // Method:
    //     SetStreamId
    //
    // Description:
    //     Sets stream id.
    //
    // Input:
    //     const int32_t id - stream id.
    //
    //////////////////////////////////////////////////////////////////////////////
    void CEUSSConcurrentGroup::SetStreamId( const int32_t id )
    {
        m_streamId = id;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CEUSSConcurrentGroup
    //
    // Method:
    //     GetBufferSize
    //
    // Description:
    //     Returns buffer size.
    //
    // Output:
    //     uint32_t - buffer size.
    //
    //////////////////////////////////////////////////////////////////////////////
    uint32_t CEUSSConcurrentGroup::GetBufferSize() const
    {
        return m_bufferSize;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CEUSSConcurrentGroup
    //
    // Method:
    //     IsSupported
    //
    // Description:
    //     Checks if EUSS concurrent group is supported.
    //
    // Input:
    //     CMetricsDevice& device - metrics device
    //
    // Output:
    //     uint32_t - true if supported
    //
    //////////////////////////////////////////////////////////////////////////////
    bool CEUSSConcurrentGroup::IsSupported( CMetricsDevice& device )
    {
        switch( device.GetPlatformIndex() )
        {
            case GENERATION_BMG:
            case GENERATION_LNL:
            case GENERATION_PTL:
            case GENERATION_NVL:
            case GENERATION_NVLP:
            case GENERATION_CRI:
                return true;

            default:
                return false;
        }
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CEUSSConcurrentGroup
    //
    // Method:
    //     NsToTicks
    //
    // Description:
    //     Converts nanoseconds to ticks using max GPU frequency (global symbol).
    //
    // Input:
    //     const uint32_t ns           - nanoseconds
    //
    // Output:
    //     uint32_t                    - ticks
    //
    //////////////////////////////////////////////////////////////////////////////
    uint32_t CEUSSConcurrentGroup::NsToTicks( const uint32_t ns ) const
    {
        return m_gpuMaxFrequency ? ( static_cast<uint64_t>( ns ) * m_gpuMaxFrequency->ValueUInt32 ) / 1000 : 0;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CEUSSConcurrentGroup
    //
    // Method:
    //     TicksToNs
    //
    // Description:
    //     Converts ticks to nanoseconds using max GPU frequency (global symbol).
    //
    // Input:
    //     const uint32_t ticks        - ticks
    //
    // Output:
    //     uint32_t                    - nanoseconds
    //
    //////////////////////////////////////////////////////////////////////////////
    uint32_t CEUSSConcurrentGroup::TicksToNs( const uint32_t ticks ) const
    {
        return m_gpuMaxFrequency ? ( static_cast<uint64_t>( ticks ) * 1000 ) / m_gpuMaxFrequency->ValueUInt32 : 0;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CEUSSConcurrentGroup
    //
    // Method:
    //     TicksToSampleRate
    //
    // Description:
    //     Converts ticks to corresponding sample rate (EU_STALL_SAMPLING_TICKS) coefficient.
    //     Available values are <1, 7>.
    //
    // Input:
    //     const uint32_t ticks        - ticks
    //
    // Output:
    //     uint32_t                    - sample rate coefficient
    //
    //////////////////////////////////////////////////////////////////////////////
    uint32_t CEUSSConcurrentGroup::TicksToSampleRate( const uint32_t ticks ) const
    {
        if( ticks == 0 )
        {
            return 0;
        }

        constexpr uint32_t minRate = EU_STALL_SAMPLING_TICKS * EU_STALL_SAMPLING_MIN_RATE;
        constexpr uint32_t maxRate = EU_STALL_SAMPLING_TICKS * EU_STALL_SAMPLING_MAX_RATE;

        if( ticks > maxRate )
        {
            return EU_STALL_SAMPLING_MAX_RATE;
        }
        else if( ticks < minRate )
        {
            return EU_STALL_SAMPLING_MIN_RATE;
        }

        return ticks / EU_STALL_SAMPLING_TICKS;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CEUSSConcurrentGroup
    //
    // Method:
    //     CEUSSConcurrentGroup destructor
    //
    // Description:
    //     Destructor.
    //
    //////////////////////////////////////////////////////////////////////////////
    CEUSSConcurrentGroup::~CEUSSConcurrentGroup()
    {
        if( m_metricSet )
        {
            CloseIoStream();
        }

        MD_SAFE_DELETE( m_bufferOverflowInfo );
    }

} // namespace MetricsDiscoveryInternal
