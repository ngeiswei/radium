declare name 		"lfboost";
declare version 	"1.0";
declare author 		"Grame";
declare license 	"BSD";
declare copyright 	"(c)GRAME 2006";

//------------------------------------------------------------------
//	DAFX, Digital Audio Effects (Wiley ed.)
//	chapter 2 	: filters
//	section 2.3 : Equalizers
//	page 53 	: second order shelving filter design
//------------------------------------------------------------------

import("math.lib");
import("music.lib");


//----------------------low frequency boost filter -------------------------------
// lfboost(F,G)
//				F :	frequency (in Hz)
//				G : gain (in dB)
//
//--------------------------------------------------------------------------------

lfboost(F,G)	= TF2(  (1 + sqrt(2*V)*K + V*K*K) / denom,
						 2 * (V*K*K - 1) / denom,
						(1 - sqrt(2*V)*K + V*K*K) / denom,
						 2 * (K*K - 1) / denom,
						(1 - sqrt(2)*K + K*K) / denom
					 )
		with {
			V			= db2linear(G);
			K 			= tan(PI*F/SR);
			denom		= 1 + sqrt(2)*K + K*K;
		};



//====================low frequency boost process ===============================

process = vgroup("lowboost", lfboost(
								nentry("freq (Hz)", 100, 20, 150, 1),
								vslider("gain (dB)", 0, -20, 20, 0.1)
				) );
