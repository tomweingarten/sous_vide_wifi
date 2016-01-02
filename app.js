baseURL = "http://192.168.1.117/"

$(document).ready(function(){
    // Set up char data
    window.chartData = [{
        values: [],
        key: "Temperature",
        yAxis: 1,
        type: "line"
    },
    {
        values: [],
        key: "Output",
        yAxis: 2,
        type: "line"
    }
    ];
    window.startTime = new Date($.now());

    nv.addGraph(function() {
        var chart = nv.models.multiChart()
                    .margin({left: 100, right:100})  //Adjust chart margins to give the x-axis some breathing room.
                    //.useInteractiveGuideline(true)  //We want nice looking tooltips and a guideline!
                    //.transitionDuration(350)  //how fast do you want the lines to transition?
                    .showLegend(true)       //Show the legend, allowing users to turn on/off line series.
                    //.showYAxis(true)        //Show the y-axis
                    //.showXAxis(true)        //Show the x-axis
        ;

        chart.xAxis     //Chart x-axis settings
            .axisLabel('Time (m)')
            .tickFormat(function(d) { return d3.time.format('%I:%M')(new Date(d)); });//d3.format(',r'));

        chart.yAxis1     //Chart y-axis settings
            .axisLabel('Temperature (F)')
            .tickFormat(d3.format('.02f'));

        chart.yDomain2([0, 10000]);

        chart.yAxis2     //Chart y-axis settings
            .axisLabel('Output')
            .tickFormat(d3.format('.02f'));

        d3.select('#temperature-chart svg')    //Select the <svg> element you want to render the chart in.   
            .datum(window.chartData)         //Populate the <svg> element with chart data...
            .call(chart);          //Finally, render the chart!

        //Update the chart when window resizes.
        nv.utils.windowResize(function() { chart.update() });
        window.temperatureChart = chart;
        return chart;
    });

    setInterval(function(){
        $.get(baseURL+"get_status/",function(data) {
            console.log(data);
            // Set x to the number of minutes since the machine started
            x = new Date($.now());//(new Date($.now()) - startTime) / (60000);
            window.chartData[0].values.push({ x: x,
                                              y: data['temperature']});
            window.chartData[1].values.push({ x: x,
                                              y: data['output']});
            $("#current-temperature").html(data['temperature']);
            $("#current-output").html(data['output']);
            if (window.chartData[0].values.length > 1000) window.chartData[0].values.shift();
        window.temperatureChart.update();
        });
    }, 3000);

    $('#ip-address').on('change', function () {
        window.baseURL="http://"+$(this).val()+"/";
    });
    $('#target-temperature').on('change', function () {
        $.post(baseURL+"set_temperature/?value="+$(this).val());
    });
})