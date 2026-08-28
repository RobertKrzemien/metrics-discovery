/*========================== begin_copyright_notice ============================

Copyright (C) 2026 Intel Corporation

SPDX-License-Identifier: MIT

============================= end_copyright_notice ===========================*/

//     File Name:  md_euss_concurrent_group.h

//     Abstract:   C++ Metrics Discovery euss concurrent group header

#pragma once

#include "md_concurrent_group.h"

using namespace MetricsDiscovery;

namespace MetricsDiscoveryInternal
{
#define EU_STALL_SAMPLING_MAX_BUFFER_SIZE ( 512 * MD_KBYTE )
#define EU_STALL_SAMPLING_MIN_RATE        1
#define EU_STALL_SAMPLING_MAX_RATE        7
#define EU_STALL_SAMPLING_TICKS           251

    ///////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CEUSSConcurrentGroup
    //
    // Description:
    //     The EUSS metrics groups that can be collected concurrently to another group.
    //     Stores metric sets. It has IO Stream implemented.
    //
    ///////////////////////////////////////////////////////////////////////////////
    class CEUSSConcurrentGroup : public CConcurrentGroup
    {
    public:
        // API 1.15:
        virtual TCompletionCode OpenIoStream( IMetricSet_1_13* metricSet, uint32_t processId, uint32_t* nsTimerPeriod, uint32_t* eussBufferSize, TIoStreamState state ) final;
        virtual TCompletionCode ChangeIoStreamState( TIoStreamState state, uint32_t* nsTimerPeriod ) final;

        // API 1.0:
        virtual TCompletionCode   OpenIoStream( IMetricSet_1_0* metricSet, uint32_t processId, uint32_t* nsTimerPeriod, uint32_t* eussBufferSize ) final;
        virtual TCompletionCode   ReadIoStream( uint32_t* reportCount, char* reportData, uint32_t readFlags ) final;
        virtual TCompletionCode   CloseIoStream( void ) final;
        virtual TCompletionCode   WaitForReports( uint32_t milliseconds ) final;
        virtual IInformation_1_0* GetIoMeasurementInformation( uint32_t index ) final;

    public:
        // Constructor:
        CEUSSConcurrentGroup( CMetricsDevice& device, const char* name, const char* longName, const uint32_t measurementTypeMask );

        // Destructor:
        virtual ~CEUSSConcurrentGroup( void );

        CEUSSConcurrentGroup( const CEUSSConcurrentGroup& )            = delete; // Delete copy-constructor
        CEUSSConcurrentGroup& operator=( const CEUSSConcurrentGroup& ) = delete; // Delete copy-assignment operator

        // Stream:
        int32_t  GetStreamId() const;
        void     SetStreamId( const int32_t id );
        uint32_t GetBufferSize() const;

    public:
        // Static methods:
        static bool IsSupported( CMetricsDevice& device );

    private:
        // Non-API:
        uint32_t NsToTicks( const uint32_t ns ) const;
        uint32_t TicksToNs( const uint32_t ticks ) const;
        uint32_t TicksToSampleRate( const uint32_t ticks ) const;

    private:
        // Variables:
        TTypedValue_1_0* m_gpuMaxFrequency;
        CMetricSet*      m_metricSet;
        CInformation*    m_bufferOverflowInfo;
        uint32_t         m_bufferSize;
        int32_t          m_streamId;
    };

} // namespace MetricsDiscoveryInternal
